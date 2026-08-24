#!/usr/bin/env python3
"""Regression test: C++ `general` results on the qsynth_ea dataset must be
semantically equivalent to the dataset's ground truth.

This guards against a class of bug where the linear simplifier (reached from
the general path for bitwise-linear sub-expressions such as `a^b|c^d`)
silently drops terms and returns a non-equivalent, "simpler" result. See
plans/QSYNTH_EA_FIX_PLAN.md.

Usage: python diff_qsynth_ea.py [N]
N: number of expressions to check (default 100).

Exits 0 if every solved result is equivalent to ground truth, 1 otherwise.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXE = os.path.join(HERE, "build", "mba_cli.exe")
DATASET = os.path.join(
    ROOT, r"external\GAMBA\experiments\datasets\qsynth_ea.txt")
BITCOUNT = 8


def load_pairs(n):
    seen, out = set(), []
    with open(DATASET, encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "," in line:
                expr, gt = (p.strip() for p in line.split(",", 1))
            else:
                expr, gt = line, ""
            if expr and expr not in seen:
                seen.add(expr)
                out.append((expr, gt))
            if len(out) >= n:
                break
    return out


def run(cmd, timeout=30):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return r.stdout
    except subprocess.TimeoutExpired:
        return None


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    pairs = load_pairs(n)
    print(f"Checking {len(pairs)} qsynth_ea expressions "
          f"({BITCOUNT}-bit, ground-truth verification)")

    solved = 0
    mismatch = 0
    for k, (expr, gt) in enumerate(pairs):
        res = run([EXE, "general", str(BITCOUNT), expr])
        if not res:
            continue
        res = res.strip()
        if not res:
            continue
        solved += 1
        if not gt:
            continue
        v = run([EXE, "verify", str(BITCOUNT), res, gt])
        if v is None or v.strip() != "EQUIVALENT":
            mismatch += 1
            if mismatch <= 8:
                print(f"  MISMATCH [{k}] expr={expr[:80]!r}")
                print(f"      res ={res[:80]!r}")
                print(f"      gt  ={gt[:80]!r}")
                print(f"      verify={v!r}")
        if (k + 1) % 25 == 0:
            print(f"  ... {k + 1}/{len(pairs)} done "
                  f"({solved} solved, {mismatch} mismatch)")

    print(f"=== qsynth_ea: {solved} solved, {mismatch} MISMATCH ===")
    sys.exit(1 if mismatch else 0)


if __name__ == "__main__":
    main()
