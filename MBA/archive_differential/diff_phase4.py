#!/usr/bin/env python3
"""Differential test for Phase 4 (expand + factorize_sums): C++ vs Python oracle.

Usage: python diff_phase4.py [N]   (N = number of expressions, default 200)
"""
import subprocess
import sys

sys.path.insert(0, r"C:\github\SiMBA-\external\GAMBA\src\utils")
from parse import parse

EXE = r"C:\github\SiMBA-\MBA\build\mba_cli.exe"
DATASETS = [
    r"C:\github\SiMBA-\data\GAMBA\mba_obf_nonlinear.txt",
    r"C:\github\SiMBA-\data\GAMBA\mba_obf_linear.txt",
    r"C:\github\SiMBA-\data\GAMBA\syntia.txt",
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
                    if expr and expr not in seen:
                        seen.add(expr)
                        out.append(expr)
        except Exception:
            pass
    return out


def cpp(mode, expr, bc=8):
    r = subprocess.run([EXE, mode, str(bc), expr], capture_output=True, text=True)
    return r.stdout.strip()


def py_expand(expr, bc=8):
    try:
        n = parse(expr, bc, True, False, False)
        if n is None:
            return None
        n.expand()
        return n.to_string()
    except Exception:
        return None


def py_factorize(expr, bc=8):
    try:
        n = parse(expr, bc, True, False, False)
        if n is None:
            return None
        n.mark_linear()
        n.factorize_sums()
        return n.to_string()
    except Exception:
        return None


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    exprs = load_exprs()[:n]
    print(f"Testing {len(exprs)} expressions (expand + factorize)")
    exp_same = exp_diff = fac_same = fac_diff = err = 0
    skipped = 0
    for expr in exprs:
        pe = py_expand(expr)
        if pe is None:
            skipped += 1
            continue
        ce = cpp("expand", expr)
        if ce == pe:
            exp_same += 1
        else:
            exp_diff += 1
            if exp_diff <= 5:
                print(f"  EXPAND DIFF: {expr!r}\n    C++={ce!r}\n    PY ={pe!r}")

        pf = py_factorize(expr)
        cf = cpp("factorize", expr)
        if cf == pf:
            fac_same += 1
        else:
            fac_diff += 1
            if fac_diff <= 5:
                print(f"  FACTORIZE DIFF: {expr!r}\n    C++={cf!r}\n    PY ={pf!r}")
    print(f"=== expand: {exp_same} identical, {exp_diff} differ ===")
    print(f"=== factorize: {fac_same} identical, {fac_diff} differ ===")
    print(f"=== skipped (parse fail): {skipped} ===")


if __name__ == "__main__":
    main()
