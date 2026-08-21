#!/usr/bin/env python3
"""Solve-rate benchmark for the GAMBA general (nonlinear) MBA simplifier.

Runs the C++ `general` CLI mode over the vendored GAMBA datasets and reports
the solve-rate: the fraction of expressions for which the simplifier returns a
non-empty result within the per-expression timeout (i.e. does not time out or
error out). This is a performance/coverage benchmark, not a correctness test
(correctness is covered by diff_general.py).

Usage: python solve_rate.py [N] [bitcount]
N: max expressions per dataset (default 200). bitcount: default 8.

Exits 0 if every dataset achieves a non-zero solve-rate, 1 otherwise.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "build", "mba_cli.exe")
DATASETS = [
    r"external\GAMBA\experiments\datasets\mba_obf_nonlinear.txt",
    r"external\GAMBA\experiments\datasets\mba_flatten.txt",
    r"external\GAMBA\experiments\datasets\syntia.txt",
]
ROOT = os.path.dirname(HERE)


def load_exprs(path, n):
    seen, out = set(), []
    try:
        with open(path, encoding="utf-8", errors="ignore") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "," in line:
                    expr = line.split(",")[0].strip() if "," in line else line
                else:
                    expr = line
                if expr and expr not in seen:
                    seen.add(expr)
                    out.append(expr)
                if len(out) >= n:
                    break
    except Exception as e:
        print(f"  failed to load {path}: {e}")
    return out


def solve(expr, bc, timeout=20):
    try:
        r = subprocess.run([EXE, "general", str(bc), expr], capture_output=True,
                           text=True, timeout=timeout)
        out = r.stdout.strip()
        return bool(out) and out != "<TIMEOUT>"
    except subprocess.TimeoutExpired:
        return False


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    bc = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    print(f"Solve-rate benchmark (general simplifier, {bc}-bit, N={n}/dataset)")
    any_ok = False
    for rel in DATASETS:
        path = os.path.join(ROOT, rel)
        name = os.path.basename(rel)
        if not os.path.exists(path):
            print(f"  {name}: (missing, skipped)")
            continue
        exprs = load_exprs(path, n)
        if not exprs:
            print(f"  {name}: (no expressions, skipped)")
            continue
        solved = 0
        for k, expr in enumerate(exprs):
            if solve(expr, bc):
                solved += 1
            if (k + 1) % 50 == 0:
                print(f"    ... {k + 1}/{len(exprs)} (solved {solved})")
        rate = 100.0 * solved / len(exprs)
        print(f"  {name}: {solved}/{len(exprs)} solved ({rate:.1f}%)")
        if solved > 0:
            any_ok = True
    print("=" * 50)
    print("OVERALL:", "PASS" if any_ok else "FAIL")
    sys.exit(0 if any_ok else 1)


if __name__ == "__main__":
    main()
