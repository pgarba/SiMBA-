#!/usr/bin/env python3
"""Differential test for Phase 7 (LinearSimplifier): C++ vs Python oracle.

Usage: python diff_simplify.py [N]
"""
import subprocess
import sys

sys.path.insert(0, r"C:\github\SiMBA-\external\GAMBA\src")
sys.path.insert(0, r"C:\github\SiMBA-\external\GAMBA\src\utils")
from simplify import simplify_linear_mba, Metric

EXE = r"C:\github\SiMBA-\MBA\build\mba_cli.exe"
DATASETS = [
    r"C:\github\SiMBA-\external\GAMBA\experiments\datasets\mba_obf_linear.txt",
    r"C:\github\SiMBA-\external\GAMBA\experiments\datasets\mba_obf_nonlinear.txt",
    r"C:\github\SiMBA-\external\GAMBA\experiments\datasets\syntia.txt",
]


def load_exprs():
    seen = set()
    out = []
    for path in DATASETS:
        try:
            with open(path, encoding="utf-8", errors="ignore") as f:
                for line in f:
                    line = line.strip()
                    if not line or "," not in line:
                        continue
                    expr = line.split(",")[0].strip()
                    if expr and not expr.startswith("#") and expr not in seen:
                        seen.add(expr)
                        out.append(expr)
        except Exception:
            pass
    return out


def cpp(expr, bc=8):
    try:
        r = subprocess.run([EXE, "simplify", str(bc), expr], capture_output=True,
                           text=True, timeout=30)
        return r.stdout.strip()
    except subprocess.TimeoutExpired:
        return "<TIMEOUT>"


def py(expr, bc=8):
    try:
        r = subprocess.run([sys.executable, r"C:\github\SiMBA-\MBA\_py_simp.py",
                            expr, str(bc)],
                           capture_output=True, text=True, timeout=30)
        out = r.stdout.strip()
        return None if out == "<PYERR>" else out
    except subprocess.TimeoutExpired:
        return "<TIMEOUT>"


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    exprs = load_exprs()[:n]
    print(f"Testing {len(exprs)} expressions (linear simplification)")
    same = diff = skip = 0
    for expr in exprs:
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
    print(f"=== simplify: {same} identical, {diff} differ, {skip} skipped ===")


if __name__ == "__main__":
    main()
