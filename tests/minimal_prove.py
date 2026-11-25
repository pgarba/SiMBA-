#!/usr/bin/env python3
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

print("Testing minimal prove...")
exprs = load_pairs(os.path.join(ROOT, "data", "e1_2vars.txt"), 1)
print(f"Loaded {len(exprs)} expressions")
for e in exprs:
    print(f"Simplifying: {e}")
    so, pr = simplify_prove(e, "native", 32)
    print(f"  Solved: {so}, Proved: {pr}")
print("Done!")
