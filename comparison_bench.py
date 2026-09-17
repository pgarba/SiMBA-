#!/usr/bin/env python3
"""SiMBA++ vs CoBRA head-to-head on CoBRA + Saturn simplification sets.

Success criteria (per expression, per solver):
  solved   : non-empty result, different from input, fast-check EQUIVALENT
             to the ground truth (or to the input when no ground truth
             exists, plus strictly shorter than the input).
  trivial  : ground truth is identical to the input (the expression is
             already fully simplified, e.g. a single monomial `c*x0`); the
             solver returned an equivalent form (a correct no-op or a
             reformat). Counted separately so a correct no-op is not
             mislabeled "unsolved" and a superficial reformat is not counted
             as a real "solved".
  unsolved : empty result / "Skipped" / output identical to input /
             equivalent but not shorter (no ground truth case).
  wrong    : result fails the fast-check equivalence test.
  timeout  : per-expression wall-time budget exceeded.

"correct" (no real failure) = solved + trivial; "real failure" = unsolved
+ wrong + timeout.

Both solvers run as separate processes, wall-clock timed, same order,
sequential. Per-case results are logged to comparison_results.jsonl.
"""
import json
import os
import re
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SIMBA = str(ROOT / "build-linux" / "SiMBA++")
MBACL = str(ROOT / "build-linux" / "mba_cli")
# CoBRA binary: override with COBRA_BIN. Falls back to a couple of common
# build locations, then to `cobra-cli` on PATH. If none is found the CoBRA
# columns are reported as "not installed" rather than crashing.
COBRA = (
    os.environ.get("COBRA_BIN")
    or shutil.which("cobra-cli")
    or next(
        (
            str(p)
            for p in (
                Path("/tmp/CoBRA/build/tools/cobra-cli/cobra-cli"),
                Path.home() / "CoBRA/build/tools/cobra-cli/cobra-cli",
            )
            if p.exists()
        ),
        None,
    )
)
TIMEOUT = 60  # seconds per expression per solver


def norm(s: str) -> str:
    return re.sub(r"\s+", "", s or "").replace("*", "")


def run(cmd, timeout=TIMEOUT):
    t0 = time.monotonic()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        dt = (time.monotonic() - t0) * 1000.0
        return p.returncode, p.stdout, p.stderr, dt, False
    except subprocess.TimeoutExpired:
        return 124, "", "TIMEOUT", (time.monotonic() - t0) * 1000.0, True


def run_simba(expr: str, bit: int):
    rc, out, err, dt, timed_out = run([SIMBA, f"--mba={expr}", f"--bitcount={bit}"])
    if timed_out:
        return None, dt, "timeout"
    m = re.search(r"\[Simplified MBA\]\s+'(.*?)'\s*time:\s*(\d+)ms", out)
    if not m:
        return None, dt, f"no-result (rc={rc})"
    res = m.group(1)
    if res == "":
        return None, dt, "skipped/empty"
    return res, dt, "ok"


def run_cobra(expr: str, bit: int):
    if not COBRA:
        return None, 0.0, "cobra-not-installed"
    rc, out, err, dt, timed_out = run([COBRA, "--mba", expr, "--bitwidth", str(bit)])
    if timed_out:
        return None, dt, "timeout"
    res = out.strip()
    if res == "":
        return None, dt, "empty-output"
    return res, dt, "ok"


def equivalent(a: str, b: str, bit: int):
    """Returns (verdict, fallback_used): verdict in {'eq','neq','unknown'}."""
    rc, out, err, dt, _ = run([MBACL, "verify", str(bit), flat(a), flat(b)])
    if out.strip() == "EQUIVALENT":
        return "eq", False
    if out.strip() == "NOT EQUIVALENT":
        return "neq", False
    # parse failure in our verifier -> fall back to the Python evaluator
    v = py_equiv(flat(a), flat(b), bit)
    if v is None:
        return "unknown", True
    return ("eq", True) if v else ("neq", True)


def judge(expr, res, gt, bit):
    """Return (status, verify_ms) for a solver result.

    status in {trivial, solved, unsolved, wrong} (see module docstring).
    """
    if gt is not None and norm(expr) == norm(gt):
        # Already-minimal case: the only correct simplification is the
        # identity. Classify separately (verify the result is still
        # equivalent so a bad rewrite is not hidden as "trivial").
        if res is None:
            return "unsolved", 0.0
        t0 = time.monotonic()
        verdict, _ = equivalent(res, expr, bit)
        vms = (time.monotonic() - t0) * 1000.0
        if verdict == "neq":
            return "wrong", vms
        return "trivial", vms
    if res is None:
        return "unsolved", 0.0
    if norm(res) == norm(expr):
        return "unsolved", 0.0
    t0 = time.monotonic()
    ref = gt if gt else expr
    verdict, _ = equivalent(res, ref, bit)
    vms = (time.monotonic() - t0) * 1000.0
    if verdict == "neq":
        return "wrong", vms
    if verdict == "unknown":
        return "unsolved", vms
    if gt is None and len(norm(res)) >= len(norm(expr)):
        return "unsolved", vms
    return "solved", vms


def load_dataset(path):
    cases = []
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        expr, _, gt = line.rpartition(",")
        cases.append((flat(expr), flat(gt) if gt else None))
    return cases


def flat(s: str) -> str:
    return " ".join(s.split())


# ---- Python equivalence fallback (C-precedence, modular arithmetic) ----
TOK = re.compile(
    r"\s*(~|<<|>>|\^|&|\||\+|-|\*|\(|\)|0[xX][0-9a-fA-F]+|\d+|[A-Za-z_][A-Za-z0-9_]*)"
)


def _toks(s):
    out, pos = [], 0
    while pos < len(s):
        m = TOK.match(s, pos)
        if not m or m.group(1) == "":
            if s[pos:].strip() == "" and pos < len(s):
                pos += 1
                continue
            raise ValueError(f"bad token at {pos}: {s[pos:pos+20]!r}")
        out.append(m.group(1))
        pos = m.end()
    return out


class _P:
    """Recursive-descent parser producing f(vals: list[int]) -> int."""

    def __init__(self, toks, vars_):
        self.t = toks
        self.i = 0
        self.vars = vars_

    def peek(self):
        return self.t[self.i] if self.i < len(self.t) else None

    def eat(self, s=None):
        tok = self.t[self.i]
        self.i += 1
        if s is not None and tok != s:
            raise ValueError(f"expected {s}, got {tok}")
        return tok

    def expr(self):
        v = self.or_()
        if self.peek() is not None:
            raise ValueError("trailing tokens")
        return v

    def or_(self):
        v = self.xor_()
        while self.peek() == "|":
            self.eat()
            r = self.xor_()
            v = lambda vals, v=v, r=r: v(vals) | r(vals)
        return v

    def xor_(self):
        v = self.and_()
        while self.peek() == "^":
            self.eat()
            r = self.and_()
            v = lambda vals, v=v, r=r: v(vals) ^ r(vals)
        return v

    def and_(self):
        v = self.shift()
        while self.peek() == "&":
            self.eat()
            r = self.shift()
            v = lambda vals, v=v, r=r: v(vals) & r(vals)
        return v

    def shift(self):
        v = self.add()
        while self.peek() in ("<<", ">>"):
            op = self.eat()
            r = self.add()
            if op == "<<":
                v = lambda vals, v=v, r=r: v(vals) << r(vals)
            else:
                v = lambda vals, v=v, r=r: v(vals) >> r(vals)
        return v

    def add(self):
        v = self.mul()
        while self.peek() in ("+", "-"):
            op = self.eat()
            r = self.mul()
            if op == "+":
                v = lambda vals, v=v, r=r: v(vals) + r(vals)
            else:
                v = lambda vals, v=v, r=r: v(vals) - r(vals)
        return v

    def mul(self):
        v = self.unary()
        while self.peek() == "*":
            self.eat()
            r = self.unary()
            v = lambda vals, v=v, r=r: v(vals) * r(vals)
        return v

    def unary(self):
        if self.peek() == "~":
            self.eat()
            f = self.unary()
            return lambda vals, f=f: ~f(vals)
        if self.peek() == "-":
            self.eat()
            f = self.unary()
            return lambda vals, f=f: -f(vals)
        return self.primary()

    def primary(self):
        tok = self.peek()
        if tok == "(":
            self.eat()
            v = self.or_()
            self.eat(")")
            return v
        if tok is None:
            raise ValueError("unexpected end")
        if re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+", tok):
            self.eat()
            val = int(tok, 0)
            return lambda vals, c=val: c
        self.eat()
        if tok not in self.vars:
            raise ValueError(f"unknown variable {tok}")
        return lambda vals, k=self.vars.index(tok): vals[k]


def _compile(expr):
    toks = _toks(expr)
    if not toks:
        raise ValueError("empty")
    vars_ = [t for t in toks if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", t)]
    vars_ = sorted(set(vars_))
    f = _P(toks, vars_).expr()
    return f, vars_


def py_equiv(a, b, bit, samples=128):
    """Random fast-check with a solver-independent Python evaluator."""
    import random
    try:
        fa, va = _compile(a)
        fb, vb = _compile(b)
    except Exception:
        return None  # cannot judge
    mask = (1 << bit) - 1
    random.seed(12345)
    for _ in range(samples):
        vals = [random.getrandbits(bit) for _ in range(max(len(va), len(vb), 1))]
        ea = fa(vals) & mask
        eb = fb(vals) & mask
        if ea != eb:
            return False
    return True


SATURN_C = (ROOT / "evaluation/saturn/linear_mba.c").read_text()


def load_saturn():
    """Cases are (expr, gt_or_None, bitwidth)."""
    cases = []
    m = re.search(r"mba0\([^)]*\)\s*\{\s*return\s*(.*?);\s*\}", SATURN_C)
    cases.append((flat(m.group(1)), "a+b", 64))  # (a^b)+2*(a&b) -> a+b
    m = re.search(r"mba1\([^)]*\)\s*\{\s*return\s*(.*?);\s*\}", SATURN_C, re.S)
    cases.append((flat(m.group(1)), None, 64))
    m = re.search(r"mba2\([^)]*\)\s*\{\s*return\s*(.*?);\s*\}", SATURN_C, re.S)
    cases.append((flat(m.group(1)), None, 32))
    # evaluation/saturn/linear_mba.ll (saturn lift of lifted_code):
    # (x+y)<<2 + z<<1
    cases.append(("4*(x0+x1)+2*x2", "(4*x0)+(4*x1)+(2*x2)", 64))
    return cases


def bench(name, cases, default_bit, solver, solver_name, logf):
    stats = {"solved": 0, "trivial": 0, "unsolved": 0, "wrong": 0, "timeout": 0}
    times = []
    for i, case in enumerate(cases):
        expr, gt = case[0], case[1]
        bit = case[2] if len(case) > 2 else default_bit
        res, dt, note = solver(expr, bit)
        status, vms = judge(expr, res, gt, bit)
        if note == "timeout":
            status = "timeout"
        stats[status] += 1
        times.append(dt)
        logf.write(json.dumps({
            "dataset": name, "case": i, "solver": solver_name,
            "status": status, "time_ms": round(dt, 2),
            "verify_ms": round(vms, 2), "note": note,
            "result": (res or "")[:200],
        }) + "\n")
        if (i + 1) % 200 == 0:
            logf.flush()
            print(f"  [{solver_name}] {name}: {i+1}/{len(cases)} "
                  f"(solved={stats['solved']} trivial={stats['trivial']} "
                  f"unsolved={stats['unsolved']} wrong={stats['wrong']} "
                  f"timeout={stats['timeout']})", flush=True)
    times.sort()
    return {
        "n": len(cases), **stats,
        "total_s": round(sum(times) / 1000.0, 2),
        "mean_ms": round(sum(times) / len(times), 2),
        "median_ms": round(times[len(times) // 2], 2),
        "p95_ms": round(times[min(len(times) - 1, int(len(times) * 0.95))], 2),
        "max_ms": round(times[-1], 2),
    }


DATASETS = [
    ("CoBRA univariate64", load_dataset(ROOT / "data/CoBRA/univariate64.txt"), 64),
    ("CoBRA multivariate64", load_dataset(ROOT / "data/CoBRA/multivariate64.txt"), 64),
    ("CoBRA permutation64", load_dataset(ROOT / "data/CoBRA/permutation64.txt"), 64),
    ("CoBRA obfuscatorx", load_dataset(ROOT / "data/CoBRA/obfuscatorx.txt"), 64),
    ("CoBRA msimba", load_dataset(ROOT / "data/CoBRA/msimba.txt"), 64),
    ("Saturn", load_saturn(), 64),
]


def main():
    logf = open(ROOT / "comparison_results.jsonl", "w")
    table = {}
    for name, cases, bit in DATASETS:
        print(f"== {name} ({len(cases)} exprs) ==", flush=True)
        s = bench(name, cases, bit, run_simba, "simba", logf)
        c = bench(name, cases, bit, run_cobra, "cobra", logf)
        table[name] = (s, c)
        print(f"  simba: {s}")
        print(f"  cobra: {c}")
    logf.close()

    print()
    print("| Dataset | N | SiMBA solved | SiMBA trivial | SiMBA unsolved | SiMBA wrong | CoBRA solved | CoBRA trivial | CoBRA unsolved | CoBRA wrong | SiMBA total | SiMBA median | SiMBA max | CoBRA total | CoBRA median | CoBRA max |")
    print("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|")
    tot = {"n": 0, "ss": 0, "striv": 0, "su": 0, "sw": 0, "cs": 0, "ctriv": 0, "cu": 0, "cw": 0, "st": 0.0, "ct": 0.0}
    for name, (s, c) in table.items():
        print(f"| {name} | {s['n']} | {s['solved']} | {s['trivial']} | {s['unsolved']+s['timeout']} | {s['wrong']} | "
              f"{c['solved']} | {c['trivial']} | {c['unsolved']+c['timeout']} | {c['wrong']} | "
              f"{s['total_s']}s | {s['median_ms']}ms | {s['max_ms']}ms | "
              f"{c['total_s']}s | {c['median_ms']}ms | {c['max_ms']}ms |")
        tot["n"] += s["n"]
        tot["ss"] += s["solved"]; tot["striv"] += s["trivial"]; tot["su"] += s["unsolved"] + s["timeout"]; tot["sw"] += s["wrong"]
        tot["cs"] += c["solved"]; tot["ctriv"] += c["trivial"]; tot["cu"] += c["unsolved"] + c["timeout"]; tot["cw"] += c["wrong"]
        tot["st"] += s["total_s"]; tot["ct"] += c["total_s"]
    print(f"| **Total** | {tot['n']} | {tot['ss']} | {tot['striv']} | {tot['su']} | {tot['sw']} | "
          f"{tot['cs']} | {tot['ctriv']} | {tot['cu']} | {tot['cw']} | "
          f"{round(tot['st'],1)}s | - | - | {round(tot['ct'],1)}s | - | - |")
    sc = tot["ss"] + tot["striv"]
    cc = tot["cs"] + tot["ctriv"]
    print(f"\nCorrect (solved + trivial): SiMBA {sc}/{tot['n']} = {100*sc/tot['n']:.1f}%   "
          f"CoBRA {cc}/{tot['n']} = {100*cc/tot['n']:.1f}%")
    print(f"Real failures (unsolved + wrong + timeout): SiMBA {tot['su']+tot['sw']}   "
          f"CoBRA {tot['cu']+tot['cw']}")
    print(f"Total time: SiMBA {tot['st']:.1f}s   CoBRA {tot['ct']:.1f}s   "
          f"(SiMBA {tot['ct']/max(tot['st'],1e-9):.1f}x faster)")


if __name__ == "__main__":
    main()
