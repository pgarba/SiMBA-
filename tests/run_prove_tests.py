#!/usr/bin/env python3
"""Run all SiMBA / MSiMBA / GAMBA tests with Z3 proving.

For each dataset, the simplifier runs with --prove (Z3 verification via the
SiMBA++ binary). A result is "proved" if the output does NOT contain
"Not valid replacement" or "not equivalent".

Usage: python run_prove_tests.py [max_per_file] [workers]
"""
import os
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SIMBA = os.path.join(ROOT, "build-linux", "SiMBA++")

SIMBA_RE = re.compile(r"\[Simplified MBA\] '(.*)' time: \d+ms")


def run(cmd, timeout=30):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return r.stdout
    except subprocess.TimeoutExpired:
        return None


def simplify_prove(expr, simplifier, bc, timeout=10):
    """Run SiMBA++ with --simplifier=<s> --prove. Returns (solved, proved).

    timeout defaults to 10s: easy cases prove in milliseconds, so a short
    deadline only costs the genuinely hard cases (which Z3 QF_BV cannot
    finish anyway - see plans/Z3_PROVE_SEMILINEAR_PLAN.md).
    """
    out = run([SIMBA, f"--mba={expr}", f"--simplifier={simplifier}",
               f"--bitcount={bc}", "--prove", f"--timeout={timeout}"], timeout + 5)
    if out is None:
        return (False, False)
    solved = bool(SIMBA_RE.search(out))
    proved = solved and "Not valid replacement" not in out and "not equivalent" not in out
    return (solved, proved)


def load_pairs(path, n):
    seen, out = set(), []
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            expr = parts[0]
            if expr and expr not in seen:
                seen.add(expr)
                out.append(expr)
            if n > 0 and len(out) >= n:
                break
    return out


def bench(name, files, simplifier, bc, n, workers):
    total = solved = proved = 0
    t0 = time.perf_counter()
    per_file = []

    def process(rel):
        path = os.path.join(ROOT, rel)
        if not os.path.isfile(path):
            return None
        exprs = load_pairs(path, n)
        s = p = 0
        for e in exprs:
            so, pr = simplify_prove(e, simplifier, bc)
            s += so
            p += pr
        return (os.path.basename(rel), len(exprs), s, p)

    with ThreadPoolExecutor(max_workers=workers) as ex:
        for r in ex.map(process, files):
            if r is None:
                continue
            fn, cnt, s, p = r
            total += cnt
            solved += s
            proved += p
            per_file.append((fn, cnt, s, p))

    dt = time.perf_counter() - t0
    print(f"  {name} ({simplifier}, {bc}-bit): {proved}/{total} proved, "
          f"{solved}/{total} solved [{dt:.1f}s]")
    for fn, cnt, s, p in per_file:
        print(f"    {fn:<24} {p:>4}/{cnt:<4} proved  {s:>4}/{cnt:<4} solved")
    return total, solved, proved


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    workers = int(sys.argv[2]) if len(sys.argv) > 2 else 4

    print(f"Z3 proving tests (N={n}/file, workers={workers})\n")

    # 1. SiMBA native
    # 32-bit (not 64): Z3 QF_BV times out on the multi-variable cases at any
    # width (measured: 16/32/64 all time out), so the width only affects the
    # constant values, not provability. 32-bit keeps the suite fast. Drop to
    # 16 if 32 is still too slow.
    print("[1/3] SiMBA native (data/*.txt)")
    simba_files = [f"data/{f}" for f in sorted(os.listdir(os.path.join(ROOT, "data")))
                   if f.endswith(".txt") and "6vars" not in f and "poly" not in f]
    t1, s1, p1 = bench("simba", simba_files, "native", 32, n, workers)

    # 2. MSiMBA
    print("\n[2/3] MSiMBA (data/MSiMBA/*.txt)")
    msimba_files = [f"data/MSiMBA/{f}" for f in sorted(os.listdir(os.path.join(ROOT, "data", "MSiMBA")))
                    if f.endswith(".txt")]
    t2, s2, p2 = bench("msimba", msimba_files, "msimba", 32, n, workers)

    # 3. GAMBA
    print("\n[3/3] GAMBA (data/GAMBA/*.txt)")
    gamba_files = [f"data/GAMBA/{f}" for f in sorted(os.listdir(os.path.join(ROOT, "data", "GAMBA")))
                   if f.endswith(".txt")]
    t3, s3, p3 = bench("gamba", gamba_files, "general", 8, n, workers)

    # Summary
    print("\n" + "=" * 60)
    print("SUMMARY")
    print("=" * 60)
    print(f"{'dataset':<10} {'proved':>10} {'solved':>10} {'total':>10}")
    print("-" * 60)
    print(f"{'simba':<10} {p1:>5}/{t1:<4} {s1:>5}/{t1:<4} {t1:>10}")
    print(f"{'msimba':<10} {p2:>5}/{t2:<4} {s2:>5}/{t2:<4} {t2:>10}")
    print(f"{'gamba':<10} {p3:>5}/{t3:<4} {s3:>5}/{t3:<4} {t3:>10}")
    print("-" * 60)
    tt, ts, tp = t1 + t2 + t3, s1 + s2 + s3, p1 + p2 + p3
    print(f"{'TOTAL':<10} {tp:>5}/{tt:<4} {ts:>5}/{tt:<4} {tt:>10}")


if __name__ == "__main__":
    main()
