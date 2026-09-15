#!/usr/bin/env python3
"""Benchmark SiMBA++ (auto simplifier) on the CoBRA test datasets.

CoBRA (github.com/trailofbits/CoBRA) test/datasets were downloaded to
data/CoBRA/. This runner normalizes each dataset to the SiMBA database
format (one "expr,groundtruth" line per row), runs SiMBA++ --mbadb on it
(chunk-parallel), and reports solved / exact-GT-match / failed / skipped.

Usage:
    python3 tests/run_cobra_tests.py [N] [workers]
    N        = max expressions per file (default: whole file)
    workers  = parallel chunk workers (default 8)

Notes:
- Files whose content is identical to our own datasets (gamba/*) are marked
  "already-covered" and run by default too (reference numbers).
- simba/e1-e5 are the ORIGINAL (const-free) Denuvo variant; we benchmark our
  const-augmented variant separately (data/MSiMBA, 100% GT match).
- univariate64/multivariate64/pldi_poly/pldi_nonpoly/obfuscatorx are
  polynomial/nonlinear classes: the native general route only runs at
  <=16-bit, so those are run at 16-bit (their intended 64-bit identities are
  out of reach for the general route by design).
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
COBRA = os.path.join(REPO, "data", "CoBRA")
BIN = os.path.join(REPO, "build-linux", "SiMBA++")

# (name, path, bitcount, max_var_count, note)
FILES = [
    # --- NEW to us (the point of this benchmark) ---
    ("msimba (new)",        "msimba.txt",                     64, 6, "semi-linear 64-bit, AND-masked consts, no GT"),
    ("univariate64 (new)",  "univariate64.txt",               16, 6, "polynomial x0^k, no GT (16-bit: general route)"),
    ("multivariate64 (new)","multivariate64.txt",             16, 6, "polynomial x0*x1..., no GT (16-bit: general route)"),
    ("permutation64 (new)", "permutation64.txt",              64, 6, "2-var mixed, with GT"),
    ("obfuscatorx (new)",   "obfuscatorx.txt",                16, 12, "from commercial obfuscator, hex consts, with GT"),
    ("oses_fast (new)",     "oses/oses_fast.txt",             64, 6, "OSES equal-saturation, with GT"),
    ("oses_slow (new)",     "oses/oses_slow.txt",             64, 6, "OSES hard subset, with GT"),
    ("simba/blast1 (new)",  "simba/blast_dataset1.txt",       64, 6, ""),
    ("simba/blast2 (new)",  "simba/blast_dataset2.txt",       64, 6, ""),
    ("simba/pldi_linear (new)",  "simba/pldi_linear.txt",     64, 6, "PLDI'18 linear"),
    ("simba/pldi_poly (new)",    "simba/pldi_poly.txt",       16, 6, "PLDI'18 polynomial (16-bit: general)"),
    ("simba/pldi_nonpoly (new)", "simba/pldi_nonpoly.txt",    16, 6, "PLDI'18 nonpoly (16-bit: general)"),
    ("simba/test_data (new)","simba/test_data.txt",           64, 6, ""),
    # --- Already covered by us (reference runs) ---
    ("simba/e1_2vars (cov)", "simba/e1_2vars.txt",            64, 6, "original const-free variant (we have const-augmented)"),
    ("simba/e5_4vars (cov)", "simba/e5_4vars.txt",            64, 6, "original const-free variant (we have const-augmented)"),
    ("gamba/qsynth_ea (cov)","gamba/qsynth_ea.txt",           64, 6, "identical to data/GAMBA/qsynth_ea.txt"),
    ("gamba/syntia (cov)",   "gamba/syntia.txt",              64, 6, "identical to data/GAMBA/syntia.txt"),
    ("gamba/neureduce (cov)","gamba/neureduce.txt",           64, 6, "identical to data/GAMBA/neureduce.txt"),
]


def normalize(path, n=None):
    """Return list of 'expr,gt' lines (comments/empties dropped, tabs->comma)."""
    rows = []
    with open(path, errors="replace") as f:
        for line in f:
            line = line.strip().lstrip("\ufeff")
            if not line or line.startswith("#"):
                continue
            if "\t" in line:
                line = line.replace("\t", ",")
            rows.append(line)
            if n and len(rows) >= n:
                break
    return rows


SUMRE = re.compile(r"MBAs:\s*(\d+)\s*->\s*Counter:\s*\d+\s*Valid:\s*(\d+)")


def run_chunk(args):
    chunk_file, bitcount, maxvars = args
    cmd = [BIN, "--mbadb=" + chunk_file, f"--bitcount={bitcount}",
           f"--max-var-count={maxvars}", "--timeout=10", "--simplifier=auto"]
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=3600)
    out = p.stdout + p.stderr
    m = SUMRE.search(out)
    solved = int(m.group(2)) if m else 0
    total = int(m.group(1)) if m else 0
    failed = out.count("Not Valid Transformation")
    skipped = len(re.findall(r"^\[\d+\] Skipped", out, re.M))
    notexact = out.count("does not meet expected string")
    return total, solved, failed, skipped, notexact


def bench(name, relpath, bitcount, maxvars, n, workers):
    path = os.path.join(COBRA, relpath)
    if not os.path.isfile(path):
        print(f"  {name:28s} MISSING {relpath}")
        return None
    rows = normalize(path, n)
    total = len(rows)
    if total == 0:
        print(f"  {name:28s} (no rows)")
        return None
    # split into worker-sized chunks
    chunks = []
    tmp = tempfile.mkdtemp(prefix="cobra_")
    try:
        step = max(1, -(-total // workers))
        for i in range(0, total, step):
            cf = os.path.join(tmp, f"chunk_{i // step}.txt")
            with open(cf, "w") as f:
                f.write("\n".join(rows[i:i + step]) + "\n")
            chunks.append((cf, bitcount, maxvars))
        with ProcessPoolExecutor(max_workers=workers) as ex:
            results = list(ex.map(run_chunk, chunks))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    agg = [sum(x[i] for x in results) for i in range(5)]
    t, solved, failed, skipped, notexact = agg
    gt = "GT " if notexact or True else ""
    print(f"  {name:28s} {solved:5d}/{total:<5d} solved "
          f"({solved * 100.0 / total:5.1f}%)  not-exact-GT {notexact:4d}  "
          f"failed {failed:4d}  skipped {skipped:4d}")
    return dict(name=name, total=total, solved=solved, failed=failed,
                skipped=skipped, notexact=notexact)


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else None
    workers = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    print(f"CoBRA dataset benchmark (N={n or 'full'}, workers={workers})")
    print(f"binary: {BIN}")
    print()
    results = []
    for name, rel, bc, mv, note in FILES:
        r = bench(name, rel, bc, mv, n, workers)
        if r:
            r["note"] = note
            results.append(r)
    print()
    print("=" * 78)
    new = [r for r in results if "(new)" in r["name"]]
    cov = [r for r in results if "(cov)" in r["name"]]
    for label, group in (("NEW datasets", new), ("ALREADY COVERED (reference)", cov)):
        if not group:
            continue
        t = sum(r["total"] for r in group)
        s = sum(r["solved"] for r in group)
        print(f"{label}: {s}/{t} solved ({s * 100.0 / t:.1f}%)")
    print()
    print("Notes: 'solved' = fast-checked valid replacement produced (auto routing).")
    print("'not-exact-GT' = solved but string differs from the dataset ground")
    print("truth (semantically valid; canonical-form difference). 'skipped' =")
    print("gated out (var count / nonlinear at >16-bit general infeasible).")


if __name__ == "__main__":
    main()
