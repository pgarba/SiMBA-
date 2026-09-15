#!/usr/bin/env python3
# Differential test (refine) over real GAMBA dataset expressions:
# C++ mba_cli "refine" (parse + refine -> to_string) vs Python oracle.
# Usage:  python MBA/diff_refine.py [limit]
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
    seen = set()
    out = []
    for e in exprs:
        if e not in seen:
            seen.add(e)
            out.append(e)
    if limit > 0:
        out = out[:limit]
    return out


def cpp_refine(expr, bit_count):
    r = subprocess.run([MBACLII, "refine", str(bit_count), expr],
                       capture_output=True, text=True)
    return r.stdout.rstrip("\n")


def py_refine(expr, bit_count):
    root = py_parse(expr, bit_count, True, False, False)
    if root is None:
        return "<parse error>"
    root.refine()
    return root.to_string()


def semantically_equivalent(e, c, p):
    """Evaluate both result strings on random inputs; True if equal mod 2^64."""
    import random
    MOD = 1 << 64
    # Collect the variables used by the original expression.
    orig = py_parse(e, 64, True, False, False)
    if orig is None:
        return c.startswith("ERROR") and p == "<parse error>"
    vars = []
    orig.collect_variables(vars)
    if not vars:
        vars = ["x"]

    cn = py_parse(c, 64, True, False, False)
    pn = py_parse(p, 64, True, False, False)
    if cn is None or pn is None:
        return c.startswith("ERROR") and p == "<parse error>"
    cn.collect_and_enumerate_variables(vars)
    pn.collect_and_enumerate_variables(vars)
    random.seed(7)
    for _ in range(300):
        X = [random.randrange(MOD) for _ in vars]
        cv, pv = cn.eval(X), pn.eval(X)
        if (cv - pv) % MOD != 0:
            return False
    return True


def main():
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    exprs = load_exprs(limit)
    ok = equiv = mismatch = 0
    for e in exprs:
        c = cpp_refine(e, 64)
        p = py_refine(e, 64)
        if c == p:
            ok += 1
        elif semantically_equivalent(e, c, p):
            equiv += 1
        else:
            mismatch += 1
            if mismatch <= 10:
                print(f"[MISMATCH] {e[:70]!r}")
                print(f"    cpp: {c[:120]}")
                print(f"    py : {p[:120]}")
    print(f"\n{ok} identical, {equiv} equiv-only, {mismatch} MISMATCH, {len(exprs)} total")
    sys.exit(1 if mismatch else 0)


if __name__ == "__main__":
    main()
