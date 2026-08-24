#!/usr/bin/env python3
"""Benchmark: MSiMBA (multi-bit semi-linear) vs SiMBA++ native vs GAMBA port,
over the data/MSiMBA/ dataset (the MSiMBA paper's semi-linear MBAs). See README
"MSiMBA benchmark" section.

The MSiMBA dataset is 64-bit and semi-linear (constants inside bitwise
operands). The MSiMBA multi-bit algorithm is polynomial (O(2^t * N)) and solves
all of them; the GAMBA general solver is exponential in the variable count and
is infeasible for 5/6-var files; the SiMBA++ native linear simplifier cannot
handle the semi-linear cases.

For each data/MSiMBA/ file (`expr,  groundtruth` lines):
  - MSiMBA:     mba_cli msimba <bc> <expr>          (all expressions, wall-timed)
  - SiMBA++:    SiMBA++ --mba <expr> --simplifier=native --bitcount <bc>
                (first N expressions, to keep runtime reasonable)
  - GAMBA:      mba_cli general <bc> <expr>         (first N expressions;
                5/6-var files are infeasible and skipped)
  - validity vs ground truth: mba_cli verify <bc> <result> <gt>
    (100-sample fast-check; EQUIVALENT => valid)

Usage: python bench_msimba.py [N] [bitcount] [workers]
Writes MBA/bench_msimba.csv and prints a summary table.
"""
import csv
import os
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SIMBA = os.path.join(ROOT, "build-linux", "SiMBA++")
MBACL = os.path.join(ROOT, "build-linux", "mba_cli")
DATA = os.path.join(ROOT, "data", "MSiMBA")

# (name, relative path within data/MSiMBA/) - all text test files.
FILES = [
    ("e1_2vars", "e1_2vars.txt"),
    ("e1_3vars", "e1_3vars.txt"),
    ("e1_4vars", "e1_4vars.txt"),
    ("e1_5vars", "e1_5vars.txt"),
    ("e1_6vars", "e1_6vars.txt"),
    ("e2_2vars", "e2_2vars.txt"),
    ("e2_3vars", "e2_3vars.txt"),
    ("e2_4vars", "e2_4vars.txt"),
    ("e3_2vars", "e3_2vars.txt"),
    ("e3_3vars", "e3_3vars.txt"),
    ("e3_4vars", "e3_4vars.txt"),
    ("e4_2vars", "e4_2vars.txt"),
    ("e4_3vars", "e4_3vars.txt"),
    ("e4_4vars", "e4_4vars.txt"),
    ("e5_2vars", "e5_2vars.txt"),
    ("e5_3vars", "e5_3vars.txt"),
    ("e5_4vars", "e5_4vars.txt"),
]

# 5/6-var files are infeasible for the GAMBA general solver (exponential in
# the variable count); skip them for the GAMBA column.
GAMBA_SKIP = {"e1_5vars", "e1_6vars"}

SIMBA_RE = re.compile(r"\[Simplified MBA\] '(.*)' time: \d+ms")


def load_pairs(path, n):
    """First n non-comment `expr,  groundtruth` pairs (deduped by expr)."""
    seen, out = set(), []
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) >= 2:
                expr, gt = parts[0], parts[1]
            else:
                expr, gt = parts[0], ""
            if expr and expr not in seen:
                seen.add(expr)
                out.append((expr, gt))
            if n > 0 and len(out) >= n:
                break
    return out


def run_timed(cmd, timeout, stdin=None):
    t0 = time.perf_counter()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True,
                           timeout=timeout, input=stdin)
    except subprocess.TimeoutExpired:
        return None, time.perf_counter() - t0
    return r.stdout, time.perf_counter() - t0


def msimba(expr, bc):
    """MSiMBA multi-bit simplifier: mba_cli msimba. Output is the result."""
    out, dt = run_timed([MBACL, "msimba", str(bc), expr], 30)
    if out is None:
        return "", dt
    lines = [l.strip() for l in out.splitlines() if l.strip()]
    return (lines[-1] if lines else ""), dt


def simba_native(expr, bc):
    """SiMBA++ native linear simplifier. Parse the [Simplified MBA] line."""
    out, dt = run_timed(
        [SIMBA, f"--mba={expr}", "--simplifier=native",
         f"--bitcount={bc}", "--timeout=30"], 60)
    if out is None:
        return "", dt
    m = SIMBA_RE.search(out)
    return (m.group(1) if m else ""), dt


def gamba_general(expr, bc):
    """GAMBA native port: mba_cli general. Output is the result."""
    out, dt = run_timed([MBACL, "general", str(bc), expr], 30)
    if out is None:
        return "", dt
    lines = [l.strip() for l in out.splitlines() if l.strip()]
    return (lines[-1] if lines else ""), dt


def is_valid(result, gt, bc):
    out, _ = run_timed([MBACL, "verify", str(bc), result, gt], 30)
    return (out or "").strip() == "EQUIVALENT"


def bench_file(name, rel, n, bc):
    path = os.path.join(DATA, rel)
    pairs = load_pairs(path, 0)  # all expressions for MSiMBA
    s = {"file": name, "n": len(pairs),
         "msimba_time": 0.0, "simba_time": 0.0, "gamba_time": 0.0,
         "msimba_solved": 0, "msimba_valid": 0,
         "simba_solved": 0, "simba_valid": 0,
         "gamba_solved": 0, "gamba_valid": 0, "gamba_n": 0}
    subset = pairs[:n] if n > 0 else pairs
    skip_gamba = name in GAMBA_SKIP
    for k, (expr, gt) in enumerate(pairs):
        m_res, m_dt = msimba(expr, bc)
        s["msimba_time"] += m_dt
        if m_res:
            s["msimba_solved"] += 1
            if gt and is_valid(m_res, gt, bc):
                s["msimba_valid"] += 1
        if k < len(subset):
            s_res, s_dt = simba_native(expr, bc)
            s["simba_time"] += s_dt
            if s_res:
                s["simba_solved"] += 1
                if gt and is_valid(s_res, gt, bc):
                    s["simba_valid"] += 1
            if not skip_gamba:
                g_res, g_dt = gamba_general(expr, bc)
                s["gamba_time"] += g_dt
                s["gamba_n"] += 1
                if g_res:
                    s["gamba_solved"] += 1
                    if gt and is_valid(g_res, gt, bc):
                        s["gamba_valid"] += 1
        if (k + 1) % 200 == 0:
            print(f"  [{name}] {k + 1}/{len(pairs)} done", flush=True)
    return s


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    bc = int(sys.argv[2]) if len(sys.argv) > 2 else 64
    workers = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    print(f"Benchmark: MSiMBA vs SiMBA++ native vs GAMBA port "
          f"({bc}-bit, MSiMBA all, native/gamba N={n}/file, workers={workers})")

    with ThreadPoolExecutor(max_workers=workers) as ex:
        futs = [ex.submit(bench_file, name, rel, n, bc) for name, rel in FILES]
        results = [f.result() for f in futs]

    order = {name: i for i, (name, _) in enumerate(FILES)}
    results.sort(key=lambda s: order[s["file"]])

    cols = ["file", "n", "msimba_time", "simba_time", "gamba_time",
            "msimba_solved", "msimba_valid",
            "simba_solved", "simba_valid",
            "gamba_solved", "gamba_valid", "gamba_n"]
    with open(os.path.join(HERE, "bench_msimba.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        for s in results:
            w.writerow({c: s[c] for c in cols})

    print()
    print(f"{'file':<12} {'n':>5} {'msimba s':>9} {'msimba val':>11} "
          f"{'simba s':>8} {'simba val':>11} {'gamba s':>8} {'gamba val':>11}")
    for s in results:
        g = f"{s['gamba_time']:>8.1f} {s['gamba_valid']:>4}/{s['gamba_n']:<4}" \
            if s["gamba_n"] else f"{'—':>8} {'—':>11}"
        print(f"{s['file']:<12} {s['n']:>5} {s['msimba_time']:>9.1f} "
              f"{s['msimba_valid']:>4}/{s['n']:<4} "
              f"{s['simba_time']:>8.1f} {s['simba_valid']:>4}/{n:<4} {g}")
    print()
    print("wrote MBA/bench_msimba.csv")


if __name__ == "__main__":
    main()
