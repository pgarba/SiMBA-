#!/usr/bin/env python3
"""Benchmark: SiMBA++ (native linear simplifier) vs. the GAMBA native port
(general), over the data/ test files. See README "GAMBA benchmark" section.

For each data/ file (first N expressions, `expr[,groundtruth[,flag]]` lines):
  - GAMBA port:   mba_cli.exe general <bc> <expr>              (wall-timed)
  - SiMBA++:      SiMBA++.exe --mba <expr> --simplifier=native --bitcount <bc>
  - validity vs ground truth: mba_cli.exe verify <bc> <result> <gt>
    (100-sample fast-check; EQUIVALENT => valid)

Usage: python bench_simba_data.py [N] [bitcount] [workers]
Writes MBA/bench_simba_data.csv and prints a summary table.
"""
import csv
import os
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SIMBA = os.path.join(ROOT, "build", "SiMBA++.exe")
MBACL = os.path.join(HERE, "build", "mba_cli.exe")
DATA = os.path.join(ROOT, "data")

# (name, relative path within data/) - all text test files in data/.
FILES = [
    ("e1_2vars", r"e1_2vars.txt"),
    ("e1_3vars", r"e1_3vars.txt"),
    ("e1_4vars", r"e1_4vars.txt"),
    ("e1_5vars", r"e1_5vars.txt"),
    ("e1_6vars", r"e1_6vars.txt"),
    ("e2_2vars", r"e2_2vars.txt"),
    ("e2_3vars", r"e2_3vars.txt"),
    ("e2_4vars", r"e2_4vars.txt"),
    ("e3_2vars", r"e3_2vars.txt"),
    ("e3_3vars", r"e3_3vars.txt"),
    ("e3_4vars", r"e3_4vars.txt"),
    ("e4_2vars", r"e4_2vars.txt"),
    ("e4_3vars", r"e4_3vars.txt"),
    ("e4_4vars", r"e4_4vars.txt"),
    ("e5_2vars", r"e5_2vars.txt"),
    ("e5_3vars", r"e5_3vars.txt"),
    ("e5_4vars", r"e5_4vars.txt"),
    ("pldi_linear", r"pldi_dataset_linear_MBA.txt"),
    ("pldi_poly", r"pldi_dataset_poly_MBA.txt"),
    ("pldi_nonpoly", r"pldi_dataset_nonpoly_MBA.txt"),
    ("test_data", r"test_data.csv"),
    ("mbablast_ds1", r"MBA-Blast\dataset1.txt"),
    ("mbablast_ds2_8", r"MBA-Blast\dataset2_8bit.txt"),
]

SIMBA_RE = re.compile(r"\[Simplified MBA\] '(.*)' time: \d+ms")


def load_pairs(path, n):
    """First n non-comment `expr[,gt[,flag]]` pairs (deduped by expr)."""
    seen, out = set(), []
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) >= 2:
                expr, gt = parts[0], parts[1]
            else:
                expr, gt = parts[0], ""
            if expr and expr not in seen:
                seen.add(expr)
                out.append((expr, gt))
            if len(out) >= n:
                break
    return out


def run_timed(cmd, timeout, stdin=None):
    t0 = time.perf_counter()
    try:
        r = subprocess.run(cmd, capture_output=True, text=True,
                           timeout=timeout, input=stdin)
    except subprocess.TimeoutExpired:
        return None, time.perf_counter() - t0
    return r.stdout, time.perf_counter() - t0


def port_general(expr, bc):
    """GAMBA native port: mba_cli.exe general. Output is the result."""
    out, dt = run_timed([MBACL, "general", str(bc), expr], 30)
    if out is None:
        return "", dt
    lines = [l.strip() for l in out.splitlines() if l.strip()]
    return (lines[-1] if lines else ""), dt


def simba_native(expr, bc):
    """SiMBA++ native linear simplifier. Parse the [Simplified MBA] line."""
    out, dt = run_timed(
        [SIMBA, f"--mba={expr}", "--simplifier=native",
         f"--bitcount={bc}", "--timeout=30"], 60)
    if out is None:
        return "", dt
    m = SIMBA_RE.search(out)
    return (m.group(1) if m else ""), dt


def is_valid(result, gt, bc):
    out, _ = run_timed([MBACL, "verify", str(bc), result, gt], 30)
    return (out or "").strip() == "EQUIVALENT"


def bench_file(name, rel, n, bc):
    path = os.path.join(DATA, rel)
    pairs = load_pairs(path, n)
    s = {"file": name, "n": len(pairs),
         "port_time": 0.0, "simba_time": 0.0,
         "port_solved": 0, "port_valid": 0,
         "simba_solved": 0, "simba_valid": 0}
    for k, (expr, gt) in enumerate(pairs):
        p_res, p_dt = port_general(expr, bc)
        s_res, s_dt = simba_native(expr, bc)
        s["port_time"] += p_dt
        s["simba_time"] += s_dt
        if p_res:
            s["port_solved"] += 1
            if gt and is_valid(p_res, gt, bc):
                s["port_valid"] += 1
        if s_res:
            s["simba_solved"] += 1
            if gt and is_valid(s_res, gt, bc):
                s["simba_valid"] += 1
        if (k + 1) % 25 == 0:
            print(f"  [{name}] {k + 1}/{len(pairs)} done", flush=True)
    return s


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    bc = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    workers = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    print(f"Benchmark: SiMBA++ native vs GAMBA port "
          f"({bc}-bit, N={n}/file, workers={workers})")

    with ThreadPoolExecutor(max_workers=workers) as ex:
        futs = [ex.submit(bench_file, name, rel, n, bc) for name, rel in FILES]
        results = [f.result() for f in futs]

    order = {name: i for i, (name, _) in enumerate(FILES)}
    results.sort(key=lambda s: order[s["file"]])

    cols = ["file", "n", "port_time", "simba_time",
            "port_solved", "port_valid", "simba_solved", "simba_valid"]
    with open(os.path.join(HERE, "bench_simba_data.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        for s in results:
            w.writerow({c: s[c] for c in cols})

    print()
    print(f"{'file':<16} {'n':>4} {'port s':>9} {'simba s':>9} "
          f"{'port sol':>11} {'port val':>11} {'simba sol':>11} {'simba val':>11}")
    for s in results:
        print(f"{s['file']:<16} {s['n']:>4} {s['port_time']:>9.1f} {s['simba_time']:>9.1f} "
              f"{s['port_solved']:>5}/{s['n']:<4} {s['port_valid']:>4}/{s['n']:<4} "
              f"{s['simba_solved']:>5}/{s['n']:<4} {s['simba_valid']:>4}/{s['n']:<4}")
    print()
    print("wrote MBA/bench_simba_data.csv")


if __name__ == "__main__":
    main()
