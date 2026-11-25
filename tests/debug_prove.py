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

print(f"HERE: {HERE}")
print(f"ROOT: {ROOT}")
print(f"SIMBA: {SIMBA}")
print(f"SIMBA exists: {os.path.isfile(SIMBA)}")

# Try a single simplification
result = subprocess.run([SIMBA, "--mba=2*~(x|~y) - (x^y) + 2*x", "--simplifier=native", "--bitcount=32", "--prove", "--timeout=5"], capture_output=True, text=True, timeout=10)
print(f"Exit code: {result.returncode}")
print(f"Output: {result.stdout[:100]}")
