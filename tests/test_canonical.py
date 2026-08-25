#!/usr/bin/env python3
"""Ground-truth canonical test for the MSiMBA normalizer.

This is the NON-vacuous version of "canonical-form proving". At runtime the
candidate result is `res = simplify(MBA)`, so checking `simplify(MBA) ==
simplify(res)` is just idempotency and proves nothing. But the dataset ships
an INDEPENDENT ground-truth simplification for each expression, so checking

    simplify(expr)  ==  simplify(ground_truth)        (mod term order)

is a real validation of the normalizer: it exercises both soundness (the
output is equivalent to the input) and canonicity (equivalent expressions
normalize to the same string) against a source the normalizer did not produce.

If this passes ~100%, the normalizer is trustworthy enough to use as the fast
proof path for the semi-linear class (skip Z3, which times out on the
multi-variable cases at any width - see plans/Z3_PROVE_SEMILINEAR_PLAN.md).

Usage:
    python3 tests/test_canonical.py [N_per_file] [workers] [bitcount]
Defaults: N=50 per file, 8 workers, 64-bit.
"""
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIMBA = os.path.join(ROOT, "build-linux", "SiMBA++")
DATA = os.path.join(ROOT, "data", "MSiMBA")


def simplify(expr, bc):
    try:
        r = subprocess.run(
            [SIMBA, f"--mba={expr}", "--simplifier=msimba", f"--bitcount={bc}",
             "--timeout=10"],
            capture_output=True, text=True, timeout=60, cwd=ROOT)
        m = re.search(r"\[Simplified MBA\] '(.*)' time", r.stdout)
        if m:
            return m.group(1)
        # "Skipped." means the input is already in simplest form (e.g. a pure
        # constant), so its canonical form is itself.
        if "[Simplified MBA] Skipped" in r.stdout:
            return expr.strip()
        return None
    except Exception:
        return None


def canon(s):
    """Sort top-level '+' terms so `x+C` and `C+x` compare equal."""
    if s is None:
        return None
    depth = 0
    terms, cur = [], []
    for ch in s:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "+" and depth == 0:
            terms.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    terms.append("".join(cur))
    return "+".join(sorted(terms))


def check_line(line, bc):
    line = line.strip()
    if not line or line.startswith("#"):
        return None
    parts = line.split(",", 1)
    if len(parts) != 2:
        return None
    expr, gt = parts[0].strip(), parts[1].strip()
    s1 = simplify(expr, bc)
    s2 = simplify(gt, bc)
    if s1 is None or s2 is None:
        return ("fail", expr[:40], "simplify returned None")
    exact = (s1 == s2)
    norm = (canon(s1) == canon(s2))
    if norm:
        return ("ok", expr[:40], "")
    return ("bad", expr[:40], f"{s1[:30]!r} vs {s2[:30]!r}")


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 50
    workers = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    bc = int(sys.argv[3]) if len(sys.argv) > 3 else 64

    files = sorted(f for f in os.listdir(DATA) if f.endswith(".txt"))
    print(f"Canonical test: {n}/file, {workers} workers, {bc}-bit, "
          f"{len(files)} files\n")

    tot_ok = tot_bad = tot_fail = 0
    bad_examples = []
    for f in files:
        with open(os.path.join(DATA, f)) as fh:
            lines = [l for l in fh if l.strip() and not l.startswith("#")]
        sample = lines[:n]
        with ThreadPoolExecutor(max_workers=workers) as ex:
            results = list(ex.map(lambda l: check_line(l, bc), sample))
        results = [r for r in results if r]
        ok = sum(1 for r in results if r[0] == "ok")
        bad = sum(1 for r in results if r[0] == "bad")
        fail = sum(1 for r in results if r[0] == "fail")
        tot_ok += ok
        tot_bad += bad
        tot_fail += fail
        for r in results:
            if r[0] == "bad" and len(bad_examples) < 10:
                bad_examples.append((f, r[1], r[2]))
        print(f"  {f:28s} {ok:4d}/{len(results):4d} canonical"
              f"   {bad} bad  {fail} fail")

    print("\n" + "=" * 50)
    print(f"TOTAL  {tot_ok} canonical, {tot_bad} bad, {tot_fail} fail")
    if bad_examples:
        print("\nBad examples (simplify(expr) vs simplify(ground_truth)):")
        for f, e, d in bad_examples:
            print(f"  [{f}] {e}\n      {d}")
    rate = 100.0 * tot_ok / max(1, tot_ok + tot_bad + tot_fail)
    print(f"\nCanonical rate: {rate:.1f}%")
    sys.exit(0 if tot_bad == 0 and tot_fail == 0 else 1)


if __name__ == "__main__":
    main()
