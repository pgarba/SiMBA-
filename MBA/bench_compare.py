#!/usr/bin/env python3
"""Benchmark: vendored Python GAMBA vs. native C++ port, over all GAMBA
datasets. See plans/BENCHMARK_PLAN.md for the methodology.

For each dataset file (first N expressions, `expr,groundtruth` lines):
  - C++ port:    mba_cli.exe general <bc> <expr>          (wall-timed)
  - Python GAMBA: python simplify_general.py <expr> -b <bc> (wall-timed)
  - validity vs ground truth: mba_cli.exe verify <bc> <result> <groundtruth>
    (100-sample fast-check; EQUIVALENT => valid)

Usage: python bench_compare.py [N] [bitcount]
Writes MBA/bench_results.csv and prints a summary table.
"""
import csv
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXE = os.path.join(HERE, "build", "mba_cli.exe")
PY = r"C:\Python\Python312\python.exe"
ORACLE = os.path.join(ROOT, "external", "GAMBA", "src", "simplify_general.py")

# (name, relative path) - all seven vendored datasets.
DATASETS = [
    ("mba_obf_nonlinear", r"external\GAMBA\experiments\datasets\mba_obf_nonlinear.txt"),
    ("mba_flatten", r"external\GAMBA\experiments\datasets\mba_flatten.txt"),
    ("syntia", r"external\GAMBA\experiments\datasets\syntia.txt"),
    ("mba_obf_linear", r"external\GAMBA\experiments\datasets\mba_obf_linear.txt"),
    ("qsynth_ea", r"external\GAMBA\experiments\datasets\qsynth_ea.txt"),
    ("neureduce", r"external\GAMBA\experiments\datasets\neureduce.txt"),
    ("loki_tiny", r"external\GAMBA\experiments\datasets\bonus\loki_tiny.txt"),
]

MARKER = "*** ... simplified to "


def load_pairs(path, n):
    """First n non-comment `expr,groundtruth` pairs (deduped by expr)."""
    seen, out = set(), []
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "," in line:
                expr, gt = (p.strip() for p in line.split(",", 1))
            else:
                expr, gt = line, ""
            if expr and expr not in seen:
                seen.add(expr)
                out.append((expr, gt))
            if len(out) >= n:
                break
    return out


def run_timed(cmd, timeout, stdin=None):
    import time
    t0 = time.perf_counter()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True,
                           timeout=timeout, input=stdin)
    except subprocess.TimeoutExpired:
        return None, time.perf_counter() - t0
    return r.stdout, time.perf_counter() - t0


def cpp_general(expr, bc):
    out, dt = run_timed([EXE, "general", str(bc), expr], 30)
    return (out or "").strip(), dt


def py_general(expr, bc):
    # Feed the expression via stdin: argparse would treat an expression
    # starting with '-' (e.g. '-8*~y*...') as an option on the command line.
    out, dt = run_timed([PY, ORACLE, "-b", str(bc)], 60, stdin=expr)
    if out is None:
        return "", dt
    for line in out.splitlines():
        if line.startswith(MARKER):
            return line[len(MARKER):].strip(), dt
    return "", dt


def is_valid(result, gt, bc):
    out, _ = run_timed([EXE, "verify", str(bc), result, gt], 30)
    return (out or "").strip() == "EQUIVALENT"


def bench_file(name, rel, n, bc):
    path = os.path.join(ROOT, rel)
    pairs = load_pairs(path, n)
    s = {"file": name, "n": len(pairs),
         "cpp_time": 0.0, "py_time": 0.0,
         "cpp_solved": 0, "cpp_valid": 0,
         "py_solved": 0, "py_valid": 0}
    for k, (expr, gt) in enumerate(pairs):
        c_res, c_dt = cpp_general(expr, bc)
        p_res, p_dt = py_general(expr, bc)
        s["cpp_time"] += c_dt
        s["py_time"] += p_dt
        if c_res:
            s["cpp_solved"] += 1
            if gt and is_valid(c_res, gt, bc):
                s["cpp_valid"] += 1
        if p_res:
            s["py_solved"] += 1
            if gt and is_valid(p_res, gt, bc):
                s["py_valid"] += 1
        if (k + 1) % 25 == 0:
            print(f"  [{name}] {k + 1}/{len(pairs)} done", flush=True)
    return s


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    bc = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    print(f"Benchmark: Python GAMBA vs C++ port ({bc}-bit, N={n}/dataset)")

    with ThreadPoolExecutor(max_workers=len(DATASETS)) as ex:
        futs = [ex.submit(bench_file, name, rel, n, bc) for name, rel in DATASETS]
        results = [f.result() for f in futs]

    order = {name: i for i, (name, _) in enumerate(DATASETS)}
    results.sort(key=lambda s: order[s["file"]])

    cols = ["file", "n", "cpp_time", "py_time", "cpp_solved", "cpp_valid",
            "py_solved", "py_valid"]
    with open(os.path.join(HERE, "bench_results.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        for s in results:
            w.writerow({c: s[c] for c in cols})

    print()
    print(f"{'file':<18} {'n':>4} {'C++ s':>8} {'PY s':>8} "
          f"{'C++ solved':>10} {'C++ valid':>9} {'PY solved':>10} {'PY valid':>9} {'x':>6}")
    for s in results:
        x = (s["py_time"] / s["cpp_time"]) if s["cpp_time"] > 0 else float("nan")
        print(f"{s['file']:<18} {s['n']:>4} {s['cpp_time']:>8.1f} {s['py_time']:>8.1f} "
              f"{s['cpp_solved']:>5}/{s['n']:<4} {s['cpp_valid']:>4}/{s['n']:<4} "
              f"{s['py_solved']:>5}/{s['n']:<4} {s['py_valid']:>4}/{s['n']:<4} {x:>6.1f}")
    print()
    print("wrote MBA/bench_results.csv")


if __name__ == "__main__":
    main()
