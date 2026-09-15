#!/usr/bin/env python3
"""Run the native GAMBA semantics test suite.

Usage: python run_all_tests.py [N]
N: number of trials for trial-based tests (default 100).

A test FAILs if its script crashes / exits non-zero, times out, is missing,
or reports a non-zero mismatch count. There are no silent passes: a child
that dies without producing a result is a failure, not a pass. (The old
runner ignored child exit codes, so crashed differential tests were counted
as PASS — see plans/REMAINING_WORK_PLAN.md, P0. The oracle-differential
tests are archived in archive_differential/.)

Exits 0 if all tests pass, 1 otherwise.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# (name, script, takes_N)
TESTS = [
    # Native-only Tier 2 semantics (first-class >> / / % nodes): random-value
    # eval against a Python unsigned reference. Not a differential test (the
    # vendored oracle is not modified for Tier 2).
    ("tier2_semantics", "test_tier2_semantics.py", True),
]


def run_test(name, script, takes_n, n):
    args = [sys.executable, os.path.join(HERE, script)]
    if takes_n:
        args.append(str(n))
    print(f"\n===== {name} =====")
    try:
        r = subprocess.run(args, capture_output=True, text=True, timeout=600)
        out = r.stdout + r.stderr
    except subprocess.TimeoutExpired as e:
        partial = (e.stdout or "") + (e.stderr or "")
        if partial:
            for line in partial.splitlines()[-10:]:
                print("  " + line.strip())
        print(f"  TIMEOUT after 600 s")
        return False
    # Print the summary lines (those with counts).
    for line in out.splitlines():
        if re.search(r"identical|differ|skipped|VERIFIED|PASS|FAIL|MISMATCH|=== ", line, re.I):
            print("  " + line.strip())
    # Honest verdict: a non-zero exit (crash, assertion, mismatch, missing
    # binary) is a FAIL. Belt and braces: a non-zero reported mismatch count
    # is a FAIL even if the script exits 0.
    if r.returncode != 0:
        print(f"  child exited with code {r.returncode}")
        return False
    m = re.search(r"(\d+)\s+(?:MISMATCH|REAL-?BUG)", out, re.I)
    if m and int(m.group(1)) > 0:
        return False
    return True


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    print(f"Running GAMBA native test suite (N={n})")
    results = {}
    for name, script, takes_n in TESTS:
        if not os.path.exists(os.path.join(HERE, script)):
            print(f"\n===== {name} =====\n  (script missing — FAIL, not skip)")
            results[name] = False
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
