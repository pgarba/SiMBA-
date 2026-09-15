#!/usr/bin/env python3
# Differential test (parser) over real GAMBA dataset expressions.
# Usage:  python MBA/diff_parse_dataset.py [limit]
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMBA = os.path.join(REPO, "external", "GAMBA")
sys.path.insert(0, os.path.join(GAMBA, "src", "utils"))
from parse import parse as py_parse  # noqa: E402

MBACLII = os.path.join(REPO, "MBA", "build", "mba_cli.exe")

DATASETS = [
    "mba_obf_nonlinear.txt",
    "mba_obf_linear.txt",
    "mba_flatten.txt",
    "syntia.txt",
    "qsynth_ea.txt",
]


def load_exprs(limit):
    exprs = []
    for name in DATASETS:
        path = os.path.join(GAMBA, "experiments", "datasets", name)
        if not os.path.exists(path):
            continue
        with open(path, "rt") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                e = line.split(",")[0].strip()
                if e:
                    exprs.append(e)
    # de-dup, preserve order
    seen = set()
    out = []
    for e in exprs:
        if e not in seen:
            seen.add(e)
            out.append(e)
    if limit > 0:
        out = out[:limit]
    return out


def cpp_parse(expr, bit_count):
    r = subprocess.run([MBACLII, "parse", str(bit_count), expr],
                       capture_output=True, text=True)
    return r.stdout.rstrip("\n")


def py_parse_str(expr, bit_count):
    root = py_parse(expr, bit_count, True, False, False)
    if root is None:
        return "<parse error>"
    return root.to_string()


def main():
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    exprs = load_exprs(limit)
    ok = diff = 0
    for e in exprs:
        c = cpp_parse(e, 64)
        p = py_parse_str(e, 64)
        match = (c.startswith("ERROR") and p == "<parse error>") or (c == p)
        if match:
            ok += 1
        else:
            diff += 1
            if diff <= 10:
                print(f"[DIFF] {e[:80]!r}")
                print(f"    cpp: {c[:120]}")
                print(f"    py : {p[:120]}")
    print(f"\n{ok} ok, {diff} diff, {len(exprs)} total")
    sys.exit(1 if diff else 0)


if __name__ == "__main__":
    main()
