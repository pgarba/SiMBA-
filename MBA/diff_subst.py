#!/usr/bin/env python3
"""Differential test for Phase 5 (substitution): C++ vs Python oracle.

Usage: python diff_subst.py [N]
"""
import subprocess
import sys

sys.path.insert(0, r"C:\github\SiMBA-\external\GAMBA\src\utils")
from parse import parse

EXE = r"C:\github\SiMBA-\MBA\build\mba_cli.exe"
DATASETS = [
    r"C:\github\SiMBA-\external\GAMBA\experiments\datasets\mba_obf_nonlinear.txt",
    r"C:\github\SiMBA-\external\GAMBA\experiments\datasets\mba_obf_linear.txt",
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
                    if expr and expr not in seen:
                        seen.add(expr)
                        out.append(expr)
        except Exception:
            pass
    return out


def cpp(expr, bc=8):
    r = subprocess.run([EXE, "subst", str(bc), expr], capture_output=True, text=True)
    return r.stdout.strip()


def py(expr, bc=8):
    try:
        n = parse(expr, bc, True, False, False)
        if n is None:
            return None
        sub = n.get_node_for_substitution([])
        if sub is not None:
            n.substitute_all_occurences(sub, "t")
        return n.to_string()
    except Exception:
        return None


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    exprs = load_exprs()[:n]
    print(f"Testing {len(exprs)} expressions (substitution)")
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
    print(f"=== substitution: {same} identical, {diff} differ, {skip} skipped ===")


if __name__ == "__main__":
    main()
