#!/usr/bin/env python3
"""Differential test for Phase 8 (GeneralSimplifier): C++ vs Python oracle.

Reads the nonlinear dataset (expr,groundtruth lines; '#' lines skipped) and,
for each expression, runs both the C++ `general` CLI mode and the Python
simplify_general.simplify_mba oracle, comparing the results.

Usage: python diff_general.py [N]
"""
import subprocess
import sys

EXE = r"C:\github\SiMBA-\MBA\build\mba_cli.exe"
PY_HELPER = r"C:\github\SiMBA-\MBA\_py_general.py"
DATASET = r"C:\github\SiMBA-\external\GAMBA\experiments\datasets\mba_obf_nonlinear.txt"
BITCOUNT = 8


def load_exprs():
    seen = set()
    out = []
    try:
        with open(DATASET, encoding="utf-8", errors="ignore") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "," not in line:
                    continue
                expr = line.split(",")[0].strip()
                if expr and expr not in seen:
                    seen.add(expr)
                    out.append(expr)
    except Exception as e:
        print(f"failed to load dataset: {e}")
    return out


def cpp(expr, bc=BITCOUNT):
    try:
        r = subprocess.run([EXE, "general", str(bc), expr], capture_output=True,
                           text=True, timeout=30)
        return r.stdout.strip()
    except subprocess.TimeoutExpired:
        return "<TIMEOUT>"


def py(expr, bc=BITCOUNT):
    try:
        r = subprocess.run([sys.executable, PY_HELPER, expr, str(bc)],
                           capture_output=True, text=True, timeout=30)
        out = r.stdout.strip()
        return None if out == "<PYERR>" else out
    except subprocess.TimeoutExpired:
        return "<TIMEOUT>"


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    exprs = load_exprs()[:n]
    print(f"Testing {len(exprs)} expressions (general simplification, {BITCOUNT}-bit)")
    same = diff = skip = 0
    for k, expr in enumerate(exprs):
        pe = py(expr)
        if pe is None:
            skip += 1
            continue
        ce = cpp(expr)
        if ce == pe:
            same += 1
        else:
            diff += 1
            if diff <= 8:
                print(f"  DIFF: {expr!r}\n    C++={ce!r}\n    PY ={pe!r}")
        if (k + 1) % 25 == 0:
            print(f"  ... {k + 1}/{len(exprs)} done "
                  f"({same} identical, {diff} differ, {skip} skipped)")
    print(f"=== general: {same} identical, {diff} differ, {skip} skipped ===")


if __name__ == "__main__":
    main()
