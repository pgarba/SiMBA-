#!/usr/bin/env python3
"""Extract the 6 failing qsynth_ea expressions (with ground truth) to a file
and stage-bisect case 3 with mba_cli modes + verify."""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bench_compare import ROOT, EXE, load_pairs, cpp_general, is_valid

bc = 8
pairs = load_pairs(os.path.join(ROOT, r"external\GAMBA\experiments\datasets\qsynth_ea.txt"), 100)
bad = [3, 52, 88, 91, 93, 98]

with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "qsynth_ea_failing.txt"), "w", encoding="utf-8") as f:
    for k in bad:
        f.write(f"{pairs[k][0]},{pairs[k][1]}\n")
print("wrote MBA/qsynth_ea_failing.txt")


def verify(a, b):
    r = subprocess.run([EXE, "verify", str(bc), a, b], capture_output=True, text=True, timeout=30)
    return (r.stdout or "").strip()


expr, gt = pairs[bad[0]]
print(f"\n=== stage bisect, case index {bad[0]} (bc={bc}) ===")
for mode in ["parse", "refine", "polish", "expand", "factorize", "subst", "general"]:
    r = subprocess.run([EXE, mode, str(bc), expr], capture_output=True, text=True, timeout=40)
    out = (r.stdout or "").strip()
    if not out:
        print(f"  {mode:<10} -> (empty)")
        continue
    v = verify(expr, out)
    tag = "OK " if v == "EQUIVALENT" else "FAIL"
    print(f"  {mode:<10} -> {tag}  ({out[:70]!r})")
