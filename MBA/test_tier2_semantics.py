#!/usr/bin/env python3
"""Native-only Tier 2 semantics test for the GAMBA C++ port.

Verifies that the first-class RSHIFT/UDIV/UREM operator nodes (built when the
Tier 1 bit desugar does not apply) evaluate with exact unsigned semantics
mod 2^B, by comparing the native mba_cli `eval` against a Python unsigned
reference for random values.

Usage:
    python MBA/test_tier2_semantics.py [trials]
(trials = random-value trials per expression; default 500. bitCount is fixed
at 8; run with a larger bitCount manually if desired.)
"""
import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CLI = os.path.join(HERE, "build", "mba_cli.exe")


def mba_eval(bc, expr, values):
    """Run mba_cli eval; returns (varnames, result)."""
    out = subprocess.run(
        [CLI, "eval", str(bc), expr, ",".join(str(v) for v in values)],
        capture_output=True, text=True, check=True,
    ).stdout.strip().splitlines()
    varnames = out[0].split(",") if out and out[0] != "" else []
    result = int(out[-1].split()[-1]) if out else None
    return varnames, result


def main():
    trials = int(sys.argv[1]) if len(sys.argv) > 1 else 500
    B = 8  # Fixed bit width (the general simplifier is exponential in width).
    M = 1 << B
    rng = random.Random(1234)

    def udiv(x, y):
        x, y = x % M, y % M
        return 0 if y == 0 else (x // y) % M

    def urem(x, y):
        x, y = x % M, y % M
        return 0 if y == 0 else (x % y) % M

    def ursh(x, y):
        x, y = x % M, y % M
        return 0 if y >= B else (x >> y) % M

    # Each case: (expr, needs_b, ref(a, b)). The reference uses unsigned
    # semantics mod 2^B. `needs_b` indicates whether the expression has a `b`
    # variable (so a second random value is supplied).
    cases = [
        ("a / 3", False, lambda a, b: udiv(a, 3)),
        ("a % 3", False, lambda a, b: urem(a, 3)),
        ("a / b", True, lambda a, b: udiv(a, b)),
        ("a % b", True, lambda a, b: urem(a, b)),
        ("a >> b", True, lambda a, b: ursh(a, b)),
        ("(a+b) / 5", True, lambda a, b: udiv((a + b) % M, 5)),
        ("(a+b) % 7", True, lambda a, b: urem((a + b) % M, 7)),
        ("a / (b+2)", True, lambda a, b: udiv(a, (b + 2) % M)),
    ]

    total = 0
    mismatch = 0
    for expr, needs_b, ref in cases:
        for _ in range(trials):
            if needs_b:
                vals = [rng.randrange(M), rng.randrange(M)]
            else:
                vals = [rng.randrange(M)]
            varnames, got = mba_eval(B, expr, vals)
            mapping = dict(zip(varnames, vals))
            a = mapping.get("a", vals[0])
            b = mapping.get("b", vals[1] if len(vals) > 1 else 0)
            want = ref(a, b)
            total += 1
            if got != want:
                mismatch += 1
                if mismatch <= 10:
                    print(f"MISMATCH {expr} a={a} b={b} got={got} want={want}")
    print(f"Tier 2 semantics: {total} checks, {mismatch} MISMATCH (bitCount={B})")
    return 1 if mismatch else 0


if __name__ == "__main__":
    sys.exit(main())
