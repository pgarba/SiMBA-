#!/usr/bin/env python3
"""Debug: which qsynth_ea results are solved but not valid vs ground truth?"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bench_compare import ROOT, EXE, load_pairs, cpp_general, is_valid

bc = 8
pairs = load_pairs(os.path.join(ROOT, r"data\GAMBA\qsynth_ea.txt"), 100)
for k, (expr, gt) in enumerate(pairs):
    res, _ = cpp_general(expr, bc)
    if not res:
        continue
    if not is_valid(res, gt, bc):
        print(f"[{k}] EXPR: {expr[:100]}")
        print(f"     GT  : {gt[:100]}")
        print(f"     RES : {res[:100]}")
        # why? show verify output
        r = subprocess.run([EXE, "verify", str(bc), res, gt], capture_output=True, text=True, timeout=30)
        print(f"     VERIFY OUT: {r.stdout.strip()[:200]}")
        print()
