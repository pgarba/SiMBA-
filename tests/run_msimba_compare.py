#!/usr/bin/env python3
"""
Compare SiMBA native linear fitter vs GAMBA general simplifier on the
data/MSiMBA dataset.

For each file:
  1. Run the GAMBA general simplifier (mba_cli generalbatch) on ALL lines.
  2. Run the SiMBA native linear fitter (SiMBA++ --simplify-expected) on a
     subset (default 100 lines) to keep runtime reasonable.
  3. Report: simplified / left / failed counts for each algorithm.

Usage:
  python3 tests/run_msimba_compare.py [bitcount] [native_subset]
  Defaults: bitcount=64, native_subset=100
"""

import sys
import os
import re
import subprocess
import time


class bcolors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKCYAN = '\033[96m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'


def find_binary(name, candidates):
    """Find the first existing binary from the candidate paths."""
    for c in candidates:
        if os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    return None


def extract_expr(line):
    """Extract the MBA expression (first comma-separated field)."""
    line = line.strip()
    if not line or line.startswith('#'):
        return None
    # The format is: <expr>,  <groundtruth>
    # But the expr itself may contain commas in some edge cases, so we
    # split on the LAST comma that is followed by whitespace + digits.
    # In practice, the ground truth is always a simple linear form, so
    # splitting on the first comma works for this dataset.
    idx = line.find(',')
    if idx < 0:
        return line
    return line[:idx].strip()


def extract_groundtruth(line):
    """Extract the ground truth (second comma-separated field)."""
    line = line.strip()
    idx = line.find(',')
    if idx < 0:
        return None
    return line[idx + 1:].strip()


def run_gamba_batch(mba_cli, bitcount, exprs_file, timeout=120):
    """Run mba_cli generalbatch on a file of expressions. Returns list of results."""
    cmd = [mba_cli, 'generalbatch', str(bitcount), exprs_file]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        lines = result.stdout.strip().split('\n') if result.stdout.strip() else []
        return lines
    except subprocess.TimeoutExpired:
        return None
    except Exception as e:
        print(f"  Error running mba_cli: {e}")
        return None


def run_native_single(simba, bitcount, expr):
    """Run SiMBA++ --simplify-expected on a single expression. Returns the simplified form or None."""
    cmd = [simba, '--simplify-expected', '-fastcheck',
           f'-bitcount={bitcount}', '-checklinear=true', f'-mba', expr]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        output = result.stdout + result.stderr
        # Look for [Simplified MBA] '<result>'
        m = re.search(r'\[Simplified MBA\]\s+\'([^\']*)\'', output)
        if m:
            return m.group(1)
        return None
    except subprocess.TimeoutExpired:
        return None
    except Exception:
        return None


def run_msimba_single(mba_cli, bitcount, expr):
    """Run mba_cli msimba on a single expression. Returns the simplified form or None."""
    cmd = [mba_cli, 'msimba', str(bitcount), expr]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        output = result.stdout.strip()
        if output:
            return output
        return None
    except subprocess.TimeoutExpired:
        return None
    except Exception:
        return None


def normalize(expr):
    """Normalize an expression for comparison (strip whitespace)."""
    if expr is None:
        return None
    return re.sub(r'\s+', '', expr.strip())


def main():
    bitcount = int(sys.argv[1]) if len(sys.argv) > 1 else 64
    native_subset = int(sys.argv[2]) if len(sys.argv) > 2 else 100

    # Find binaries
    simba = find_binary('SiMBA++', [
        '../build/SiMBA++', '../build-linux/SiMBA++',
        os.path.join(os.path.dirname(__file__), '..', 'build', 'SiMBA++'),
    ])
    mba_cli = find_binary('mba_cli', [
        '../build-linux/mba_cli', '../build/mba_cli',
        os.path.join(os.path.dirname(__file__), '..', 'build-linux', 'mba_cli'),
        os.path.join(os.path.dirname(__file__), '..', 'MBA', 'build', 'mba_cli'),
    ])

    if not simba:
        print(bcolors.FAIL + "Error: SiMBA++ binary not found" + bcolors.ENDC)
        sys.exit(1)
    if not mba_cli:
        print(bcolors.FAIL + "Error: mba_cli binary not found" + bcolors.ENDC)
        sys.exit(1)

    print(f"{bcolors.HEADER}{'=' * 70}")
    print(f"  MSiMBA Dataset: SiMBA Native vs GAMBA General Simplifier")
    print(f"  Bitcount: {bitcount}  |  Native subset: {native_subset}/file")
    print(f"  SiMBA++: {simba}")
    print(f"  mba_cli: {mba_cli}")
    print(f"{'=' * 70}{bcolors.ENDC}\n")

    data_dir = os.path.join(os.path.dirname(__file__), '..', 'data', 'MSiMBA')
    if not os.path.isdir(data_dir):
        print(bcolors.FAIL + f"Error: data directory not found: {data_dir}" + bcolors.ENDC)
        sys.exit(1)

    files = sorted([f for f in os.listdir(data_dir) if f.endswith('.txt')])

    # Per-file GAMBA timeout (seconds).
    gamba_timeout = 60  # seconds per file
    # For files with 4+ vars, limit the GAMBA batch to this many expressions.
    # 5/6-var files are infeasible (exponential in var count) — skip them.
    gamba_var_limit = {
        '4vars': 50,   # ~440ms/expr -> ~22s for 50
    }
    gamba_skip = {'5vars', '6vars', 'mba_obf_linear', 'msimba.txt'}  # skip infeasible/slow

    # Overall totals
    tot = {
        'gamba_simplified': 0, 'gamba_left': 0, 'gamba_failed': 0, 'gamba_total': 0,
        'native_simplified': 0, 'native_left': 0, 'native_failed': 0, 'native_total': 0,
        'msimba_simplified': 0, 'msimba_left': 0, 'msimba_failed': 0, 'msimba_total': 0,
        'msimba_correct': 0,
    }

    for filename in files:
        filepath = os.path.join(data_dir, filename)
        with open(filepath, 'r') as f:
            lines = [l.rstrip('\n') for l in f if l.strip() and not l.startswith('#')]

        if not lines:
            continue

        exprs = []
        groundtruths = []
        for line in lines:
            e = extract_expr(line)
            g = extract_groundtruth(line)
            if e:
                exprs.append(e)
                groundtruths.append(g)

        if not exprs:
            continue

        # Skip infeasible files (5/6 vars)
        if any(skip in filename for skip in gamba_skip):
            print(f"{bcolors.BOLD}--- {filename} ({len(exprs)} expressions) ---{bcolors.ENDC}")
            print(f"  {bcolors.WARNING}[SKIP] 5/6-var expressions are infeasible for the general solver{bcolors.ENDC}\n")
            continue

        print(f"{bcolors.BOLD}--- {filename} ({len(exprs)} expressions) ---{bcolors.ENDC}")

        # --- GAMBA general simplifier ---
        # Limit expression count for high-var files (exponential blowup)
        gamba_exprs = exprs
        gamba_limited = False
        for key, limit in gamba_var_limit.items():
            if key in filename:
                gamba_exprs = exprs[:limit]
                gamba_limited = True
                break

        # Write expressions to a temp file
        tmpfile = f'/tmp/msimba_{filename}'
        with open(tmpfile, 'w') as tf:
            for e in gamba_exprs:
                tf.write(e + '\n')

        t0 = time.time()
        gamba_results = run_gamba_batch(mba_cli, bitcount, tmpfile, timeout=gamba_timeout)
        gamba_time = time.time() - t0

        g_simplified = 0
        g_left = 0
        g_failed = 0
        g_correct = 0

        if gamba_results is None:
            print(f"  {bcolors.FAIL}[GAMBA]   TIMEOUT ({gamba_timeout}s) — skipped{bcolors.ENDC}")
            tot['gamba_total'] += len(exprs)  # count as unprocessed
        else:
            for i, expr in enumerate(gamba_exprs):
                result = gamba_results[i] if i < len(gamba_results) else ''
                result_norm = normalize(result)
                expr_norm = normalize(expr)
                gt_norm = normalize(groundtruths[i]) if groundtruths[i] else None

                if not result_norm:
                    g_failed += 1
                elif result_norm == expr_norm:
                    g_left += 1
                else:
                    g_simplified += 1
                    if gt_norm and result_norm == gt_norm:
                        g_correct += 1

        tot['gamba_simplified'] += g_simplified
        tot['gamba_left'] += g_left
        tot['gamba_failed'] += g_failed
        tot['gamba_total'] += len(gamba_exprs)

        limit_note = f" (limited to {len(gamba_exprs)}/{len(exprs)})" if gamba_limited else ""
        print(f"  {bcolors.OKCYAN}[GAMBA]   {g_simplified} simplified, "
              f"{g_left} left, {g_failed} failed "
              f"({g_correct} match ground truth) "
              f"[{gamba_time:.1f}s]{limit_note}{bcolors.ENDC}")

        # --- MSiMBA multi-bit simplifier (full dataset) ---
        m_simplified = 0
        m_left = 0
        m_failed = 0
        m_correct = 0

        t0 = time.time()
        for i, expr in enumerate(exprs):
            result = run_msimba_single(mba_cli, bitcount, expr)
            result_norm = normalize(result)
            expr_norm = normalize(expr)
            gt_norm = normalize(groundtruths[i]) if groundtruths[i] else None

            if result_norm is None or result_norm == '':
                m_failed += 1
            elif result_norm == expr_norm:
                m_left += 1
            else:
                m_simplified += 1
                if gt_norm and result_norm == gt_norm:
                    m_correct += 1

            if (i + 1) % 200 == 0:
                print(f"\r  [msimba] {i + 1}/{len(exprs)}", end='', flush=True)

        msimba_time = time.time() - t0
        print(f"\r  [msimba] {len(exprs)}/{len(exprs)} done "
              f"[{msimba_time:.1f}s]{' ' * 20}")

        tot['msimba_simplified'] += m_simplified
        tot['msimba_left'] += m_left
        tot['msimba_failed'] += m_failed
        tot['msimba_total'] += len(exprs)
        tot['msimba_correct'] += m_correct

        print(f"  {bcolors.OKGREEN}[MSIMBA]  {m_simplified} simplified, "
              f"{m_left} left, {m_failed} failed "
              f"({m_correct} match ground truth) "
              f"[{msimba_time:.1f}s]{bcolors.ENDC}")

        # --- SiMBA native linear fitter (subset) ---
        subset = exprs[:native_subset]
        n_simplified = 0
        n_left = 0
        n_failed = 0
        n_correct = 0

        t0 = time.time()
        for i, expr in enumerate(subset):
            result = run_native_single(simba, bitcount, expr)
            result_norm = normalize(result)
            expr_norm = normalize(expr)
            gt_norm = normalize(groundtruths[i]) if groundtruths[i] else None

            if result_norm is None or result_norm == '':
                n_failed += 1
            elif result_norm == expr_norm:
                n_left += 1
            else:
                n_simplified += 1
                if gt_norm and result_norm == gt_norm:
                    n_correct += 1

            # Progress
            if (i + 1) % 25 == 0:
                print(f"\r  [native] {i + 1}/{len(subset)}", end='', flush=True)

        native_time = time.time() - t0
        print(f"\r  [native] {len(subset)}/{len(subset)} done "
              f"[{native_time:.1f}s]{' ' * 20}")

        tot['native_simplified'] += n_simplified
        tot['native_left'] += n_left
        tot['native_failed'] += n_failed
        tot['native_total'] += len(subset)

        print(f"  {bcolors.OKBLUE}[NATIVE]  {n_simplified} simplified, "
              f"{n_left} left, {n_failed} failed "
              f"({n_correct} match ground truth) "
              f"[{native_time:.1f}s, {len(subset)}/{len(exprs)} lines]{bcolors.ENDC}")
        print()

        # Cleanup
        if os.path.exists(tmpfile):
            os.remove(tmpfile)

    # --- Summary ---
    print(f"{bcolors.HEADER}{'=' * 70}")
    print(f"  SUMMARY")
    print(f"{'=' * 70}{bcolors.ENDC}")
    print(f"  {bcolors.BOLD}Algorithm{'/' * 1}{'Simplified':>12}{'Left':>10}{'Failed':>10}{'Total':>10}{'Rate':>10}{'GT Match':>10}{bcolors.ENDC}")
    print(f"  {'-' * 64}")

    g_rate = f"{100.0 * tot['gamba_simplified'] / tot['gamba_total']:.1f}%" if tot['gamba_total'] else "N/A"
    n_rate = f"{100.0 * tot['native_simplified'] / tot['native_total']:.1f}%" if tot['native_total'] else "N/A"
    m_rate = f"{100.0 * tot['msimba_simplified'] / tot['msimba_total']:.1f}%" if tot['msimba_total'] else "N/A"

    print(f"  {bcolors.OKCYAN}GAMBA{'/' * 5}{tot['gamba_simplified']:>12}{tot['gamba_left']:>10}{tot['gamba_failed']:>10}{tot['gamba_total']:>10}{g_rate:>10}{'':>10}{bcolors.ENDC}")
    print(f"  {bcolors.OKBLUE}NATIVE{'/' * 4}{tot['native_simplified']:>12}{tot['native_left']:>10}{tot['native_failed']:>10}{tot['native_total']:>10}{n_rate:>10}{'':>10}{bcolors.ENDC}")
    print(f"  {bcolors.OKGREEN}MSIMBA{'/' * 4}{tot['msimba_simplified']:>12}{tot['msimba_left']:>10}{tot['msimba_failed']:>10}{tot['msimba_total']:>10}{m_rate:>10}{str(tot['msimba_correct']):>10}{bcolors.ENDC}")
    print(f"  {'-' * 64}")
    print(f"  Note: NATIVE only ran on first {native_subset} lines per file "
          f"({tot['native_total']} total) due to per-expression process start-up.")
    print(f"  GAMBA ran on all {tot['gamba_total']} expressions via generalbatch.")
    print(f"  MSIMBA ran on all {tot['msimba_total']} expressions.")
    print()


if __name__ == '__main__':
    main()
