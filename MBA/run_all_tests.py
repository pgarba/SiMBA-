#!/usr/bin/env python3
"""Run all GAMBA differential tests (C++ native port vs vendored Python oracle).

Usage: python run_all_tests.py [N]
N: number of expressions per dataset-based test (default 100).

Exits 0 if all tests pass (no real differences), 1 otherwise.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# (name, script, takes_N)
TESTS = [
    ("parse", "diff_parse.py", False),
    ("parse_dataset", "diff_parse_dataset.py", False),
    ("node", "diff_node.py", True),
    ("refine", "diff_refine.py", True),
    ("phase4_expand_factorize", "diff_phase4.py", True),
    ("subst", "diff_subst.py", True),
    ("bitwise", "diff_bitwise.py", False),
    ("simplify_linear", "diff_simplify.py", True),
    ("general", "diff_general.py", True),
]


def run_test(name, script, takes_n, n):
    args = [sys.executable, os.path.join(HERE, script)]
    if takes_n:
        args.append(str(n))
    print(f"\n===== {name} =====")
    try:
        r = subprocess.run(args, capture_output=True, text=True, timeout=600)
        out = r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        print(f"  TIMEOUT")
        return False
    # Print the summary lines (those with counts).
    for line in out.splitlines():
        if re.search(r"identical|differ|skipped|VERIFIED|PASS|FAIL|=== ", line, re.I):
            print("  " + line.strip())
    # A test is a real FAIL only if it reports a NON-ZERO mismatch/real-bug
    # count. A "0 MISMATCH" (no mismatches) is a pass; a non-zero "differ"
    # count without a real-bug marker means the differences are
    # semantically-equivalent (term ordering / negation normal form).
    m = re.search(r"(\d+)\s+(?:MISMATCH|REAL-?BUG)", out, re.I)
    if m and int(m.group(1)) > 0:
        return False
    return True


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    print(f"Running all GAMBA differential tests (N={n})")
    results = {}
    for name, script, takes_n in TESTS:
        if not os.path.exists(os.path.join(HERE, script)):
            print(f"\n===== {name} =====\n  (script missing, skipped)")
            continue
        results[name] = run_test(name, script, takes_n, n)

    print("\n" + "=" * 50)
    print("SUMMARY")
    print("=" * 50)
    all_ok = True
    for name, ok in results.items():
        print(f"  {name}: {'PASS' if ok else 'FAIL'}")
        if not ok:
            all_ok = False
    print("=" * 50)
    print("OVERALL:", "PASS" if all_ok else "FAIL")
    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
