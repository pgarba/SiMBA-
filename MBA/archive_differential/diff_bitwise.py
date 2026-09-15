#!/usr/bin/env python3
"""Differential test for Phase 6 (BitwiseFactory): C++ vs Python oracle.

Tests all truth vectors for vnumber 1, 2, and a sample for vnumber 3.
Usage: python diff_bitwise.py
"""
import itertools
import subprocess
import sys

sys.path.insert(0, r"C:\github\SiMBA-\external\GAMBA\src\bitwise-factory")
from create_bitwise import create_bitwise

EXE = r"C:\github\SiMBA-\MBA\build\mba_cli.exe"


def cpp(vnumber, vec):
    r = subprocess.run(
        [EXE, "bitwise", str(vnumber), ",".join(str(v) for v in vec)],
        capture_output=True, text=True)
    return r.stdout.strip()


def main():
    same = diff = 0
    for vnumber in (1, 2, 3):
        n = 2 ** vnumber
        # All truth vectors for vnumber 1,2; a sample for vnumber 3.
        if vnumber <= 2:
            vectors = [list(p) for p in itertools.product((0, 1), repeat=n)]
        else:
            vectors = [list(p) for p in itertools.product((0, 1), repeat=n)][:40]
        for vec in vectors:
            pe = create_bitwise(vnumber, list(vec))
            ce = cpp(vnumber, vec)
            if ce == pe:
                same += 1
            else:
                diff += 1
                if diff <= 8:
                    print(f"  DIFF v={vnumber} vec={vec}\n    C++={ce!r}\n    PY ={pe!r}")
    print(f"=== bitwise: {same} identical, {diff} differ ===")


if __name__ == "__main__":
    main()
