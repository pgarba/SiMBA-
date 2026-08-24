#!/usr/bin/env python3
# Semantic test for the >>, /, % bit-desugaring in the vendored Python GAMBA
# oracle. The differential suite (diff_parse.py) only checks that the C++ port
# and the oracle *agree*; this test checks that the desugaring is actually
# *correct* — that the desugared tree evaluates to the true integer result for
# `a >> k`, `a / 2^k` and `a % 2^k` over random and edge values.
#
# Usage:  python MBA/test_divrem_semantics.py
import os
import random
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMBA_UTILS = os.path.join(REPO, "external", "GAMBA", "src", "utils")
sys.path.insert(0, GAMBA_UTILS)
from parse import parse  # noqa: E402

BIT_COUNTS = [1, 8, 16, 64]
RNG = random.Random(0xC0FFEE)


def eval_desugared(expr, bit_count, a):
    """Parse expr (referencing the full variable `a`), feed the individual bits
    of `a` to the bit-slice variables a[i] it desugars into, and return the
    evaluated value mod 2^bit_count. Returns None on a parse error."""
    root = parse(expr, bit_count, True, False, False)
    if root is None:
        return None
    vars = []
    root.collect_and_enumerate_variables(vars)
    mod = 2 ** bit_count
    X = [0] * len(vars)
    for idx, name in enumerate(vars):
        m = re.fullmatch(r"a\[(\d+)\]", name)
        if m is None:
            # A bare `a` (e.g. the k=0 identity) — feed the whole value.
            if name == "a":
                X[idx] = a % mod
                continue
            return "BADVAR:" + name
        i = int(m.group(1))
        X[idx] = (a >> i) & 1
    return root.eval(X) % mod


def check(label, got, want):
    if got != want:
        print(f"  FAIL {label}: got {got}, want {want}")
        return False
    return True


def main():
    failures = 0
    checks = 0
    for B in BIT_COUNTS:
        mod = 2 ** B
        # A spread of values: 0, all-ones, top-bit set, and random.
        values = [0, mod - 1, 1 << (B - 1), 1, (mod - 1) >> 1]
        values += [RNG.randrange(mod) for _ in range(24)]
        values = list(dict.fromkeys(values))  # dedupe, keep order

        for a in values:
            for k in range(0, B + 2):  # k = 0 .. B+1 (covers the k>=B edges)
                div = 2 ** k
                # Ground truth (unsigned, values reduced mod 2^B).
                gt_shift = (a >> k) % mod
                gt_div = (a // div) % mod
                gt_rem = (a % div) % mod

                got_shift = eval_desugared(f"a >> {k}", B, a)
                got_div = eval_desugared(f"a / {div}", B, a)
                got_rem = eval_desugared(f"a % {div}", B, a)

                if got_shift is None or got_div is None or got_rem is None:
                    print(f"  FAIL B={B} a={a} k={k}: parse error "
                          f"({got_shift!r}/{got_div!r}/{got_rem!r})")
                    failures += 1
                    continue

                checks += 1
                if not check(f"B={B} a={a} k={k}  a>>k", got_shift, gt_shift):
                    failures += 1
                checks += 1
                if not check(f"B={B} a={a} k={k}  a/2^k", got_div, gt_div):
                    failures += 1
                checks += 1
                if not check(f"B={B} a={a} k={k}  a%2^k", got_rem, gt_rem):
                    failures += 1

        # `<<` regression (must remain a ring operation, not bit-desugared).
        for a in values[:6]:
            for k in range(0, B + 1):
                got = eval_desugared(f"a << {k}", B, a)
                want = (a * (2 ** k)) % mod
                checks += 1
                if got is None or not check(f"B={B} a={a} k={k}  a<<k", got, want):
                    failures += 1

    # "MISMATCH" is the token run_all_tests.py greps for to detect a failure.
    print(f"\n{checks} checks, {failures} MISMATCH")
    print("OVERALL:", "PASS" if failures == 0 else "FAIL")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
