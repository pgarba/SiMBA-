#!/usr/bin/env python3
# Differential test: C++ mba_cli (parse) vs. vendored Python GAMBA parse().
# Usage:  python MBA/diff_parse.py [extra-expr ...]
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # repo root
GAMBA_UTILS = os.path.join(REPO, "external", "GAMBA", "src", "utils")
sys.path.insert(0, GAMBA_UTILS)
from parse import parse as py_parse  # noqa: E402

MBACLII = os.path.join(REPO, "MBA", "build", "mba_cli.exe")

CORPUS = [
    "x+x", "a&b|c", "a << 3", "a**2", "0x1F+0b101", "-a", "~(a+b)",
    "a*b*c", "(a+b)*(c-d)", "a^b^c", "x[0]&~x[1]", "5", "a-b-c", "~a",
    "a*(2**b)", "(a|b)&(a|c)", "a|b|c", "a&b&c", "a^b", "a|b", "a&b",
    "a+b", "a-b", "a*b", "a**b", "~a+b", "-a*b", "a*-1", "a*-2",
    "(a+b)**2", "2**a", "a << 1 << 2", "a**2**2", "a << b + c",
    "a + b << c", "~(a&b)", "~~a", "-(a+b)", "a + -b", "a - -b",
    "0", "1", "0x0", "0b0", "0b11111111", "0xFFFFFFFF", "18446744073709551615",
    "a", "a[0]", "a[12]", "x0", "x10", "abc", "a_b", "A", "aBc",
    "a + b - c + d", "a * b - c * d", "(a+b)", "((a))", "a | (b & c)",
    "a << 0", "a << 64", "0 << a", "1 << 63",
    "a & ~a", "a | ~a", "a ^ ~a", "~a & b", "a & b | c ^ d",
    "a + a + a", "a * a * a", "a**3", "a**1", "a**0",
    # Shift / division / remainder (Tier 1 desugaring).
    "a >> 1", "a >> 3", "a >> 0", "a >> 63", "a >> 64",
    "a / 2", "a / 4", "a / 64", "a / 1",
    "a % 2", "a % 8", "a % 64", "a % 1",
    "a / 4 + b", "a % 8 + b", "a / 4 * b", "a / 2 + b / 2",
    "a + b / 4", "a >> 1 * b",
    # Rejected: zero divisor, nested shift.
    "a % 0", "a / 0", "a >> 1 << 2",
    # Tier 2 (native-only): the C++ port now builds first-class >> / / % nodes
    # for these, so it parses them while the Python oracle still rejects them.
    # (`a >> 1 * b` already appears in the Tier 1 list above; it is listed in
    # TIER2_MORE_PERMISSIVE below, not duplicated here.)
    "a % 3", "a / 3", "a >> b", "(a+b) >> 1", "a >> -1",
    "a[3] >> 1", "a * b / c",
]

# Cases where the native C++ port is intentionally MORE permissive than the
# Python oracle: the native port builds a first-class Tier 2 operator node
# (>> / / %) where the oracle still rejects. For these, "cpp parses / py
# rejects" is the expected (OK) outcome, not a diff.
TIER2_MORE_PERMISSIVE = {
    "a % 3", "a / 3", "a >> b", "(a+b) >> 1", "a >> -1",
    "a[3] >> 1", "a * b / c", "a >> 1 * b",
}


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
    exprs = list(CORPUS) + sys.argv[1:]
    ok = diff = 0
    for e in exprs:
        c = cpp_parse(e, 64)
        p = py_parse_str(e, 64)
        cpp_rejected = c.startswith("ERROR")
        py_rejected = p == "<parse error>"
        # Both rejecting is a match; otherwise compare the strings exactly.
        match = (cpp_rejected and py_rejected) or (c == p)
        # Tier 2: the native port is intentionally more permissive (it builds a
        # first-class >> / / % node where the oracle still rejects). For these
        # cases, "cpp parses / py rejects" is the expected outcome.
        if (not match and e in TIER2_MORE_PERMISSIVE
                and not cpp_rejected and py_rejected):
            match = True
        status = "OK  " if match else "DIFF"
        if match:
            ok += 1
        else:
            diff += 1
            print(f"[{status}] {e!r}")
            print(f"    cpp: {c}")
            print(f"    py : {p}")
    print(f"\n{ok} ok, {diff} diff, {len(exprs)} total")
    sys.exit(1 if diff else 0)


if __name__ == "__main__":
    main()
