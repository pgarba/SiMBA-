#!/usr/bin/env python3
"""Profile per-expression C++ `general` timing on a dataset.
Usage: python _perf_profile.py <dataset_rel> [N]
Prints the slowest expressions (wall time) and aggregate stats.
"""
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXE = os.path.join(HERE, "build", "mba_cli.exe")
BC = 8

PERF_PAT = re.compile(
    r"iters=(\d+) refactor=(\d+) skips=(\d+) tLinear=([\d.]+) "
    r"tRefactor=([\d.]+) tSubst=([\d.]+) tTotal=([\d.]+)")


def load(path, n):
    seen, out = set(), []
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            expr = line.split(",")[0].strip()
            if expr and expr not in seen:
                seen.add(expr)
                out.append(expr)
            if len(out) >= n:
                break
    return out


def main():
    args = [a for a in sys.argv[1:] if a != "--perf"]
    perf = "--perf" in sys.argv
    rel = args[0]
    n = int(args[1]) if len(args) > 1 else 100
    exprs = load(os.path.join(ROOT, rel), n)
    env = dict(os.environ)
    if perf:
        env["MBASIMBA_PERF"] = "1"
    print(f"Profiling {len(exprs)} expressions from {rel} ({BC}-bit) perf={perf}")
    rows = []
    agg = dict(iters=0, refactor=0, skips=0, tLinear=0.0, tRefactor=0.0,
               tSubst=0.0, tTotal=0.0)
    for k, e in enumerate(exprs):
        t0 = time.perf_counter()
        r = subprocess.run([EXE, "general", str(BC), e],
                           capture_output=True, text=True, timeout=30, env=env)
        dt = time.perf_counter() - t0
        solved = bool(r.stdout.strip())
        rows.append((dt, k, solved, e))
        if perf:
            m = PERF_PAT.search(r.stderr)
            if m:
                agg["iters"] += int(m.group(1))
                agg["refactor"] += int(m.group(2))
                agg["skips"] += int(m.group(3))
                agg["tLinear"] += float(m.group(4))
                agg["tRefactor"] += float(m.group(5))
                agg["tSubst"] += float(m.group(6))
                agg["tTotal"] += float(m.group(7))
    if perf:
        print("--- PERF aggregate ---")
        print(f"  iters={agg['iters']}  refactor={agg['refactor']}  "
              f"skips={agg['skips']}  skipRate={agg['skips'] / max(1, agg['refactor']):.2f}")
        print(f"  tLinear={agg['tLinear']:.3f}  tRefactor={agg['tRefactor']:.3f}  "
              f"tSubst={agg['tSubst']:.3f}  tTotal={agg['tTotal']:.3f}")
    rows.sort(reverse=True)
    total = sum(x[0] for x in rows)
    solved = sum(1 for x in rows if x[1 + 1])
    over = sum(1 for x in rows if x[0] > 25)
    print(f"total={total:.1f}s  solved={solved}/{len(rows)}  >25s(deadline)={over}")
    print(f"--- slowest 15 ---")
    for dt, k, solved, e in rows[:15]:
        print(f"  [{k}] {dt:7.2f}s  solved={int(solved)}  len={len(e):4d}  {e[:70]}")
    print(f"--- timing histogram (s) ---")
    buckets = [0.1, 0.5, 1, 2, 5, 10, 25]
    prev = 0
    for b in buckets:
        c = sum(1 for x in rows if prev <= x[0] < b)
        print(f"  {prev:>5.1f}-{b:<5.1f}s : {c}")
        prev = b
    c = sum(1 for x in rows if x[0] >= 25)
    print(f"  >=25.0s   : {c}")


if __name__ == "__main__":
    main()
