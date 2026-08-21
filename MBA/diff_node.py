#!/usr/bin/env python3
# Differential test (Node core: mark_linear states + stats) over real GAMBA
# dataset expressions, comparing the C++ mba_cli against the Python oracle.
#
# Usage:  python MBA/diff_node.py [limit]
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMBA = os.path.join(REPO, "external", "GAMBA")
sys.path.insert(0, os.path.join(GAMBA, "src", "utils"))
from node import NodeType  # noqa: E402
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


def cpp(mode, expr, bit_count):
    r = subprocess.run([MBACLII, mode, str(bit_count), expr],
                       capture_output=True, text=True)
    return r.stdout.rstrip("\n")


def py_state_dump(node, level=0):
    lines = []
    indent = "  " * level
    sv = node.state.value
    if isinstance(sv, tuple):
        sv = sv[0]
    line = f"{indent}T{node.type.value} S{sv} LE{node.linearEnd}"
    if node.type == NodeType.CONSTANT:
        line += f" C{node.constant}"
    if node.type == NodeType.VARIABLE:
        line += f" V{node.vname} I{node._Node__vidx}"
    lines.append(line)
    for c in node.children:
        lines.extend(py_state_dump(c, level + 1))
    return lines


def py_state(expr, bit_count):
    root = py_parse(expr, bit_count, True, False, False)
    if root is None:
        return "<parse error>"
    root.mark_linear()
    return "\n".join(py_state_dump(root))


def py_stats(expr, bit_count):
    root = py_parse(expr, bit_count, True, False, False)
    if root is None:
        return "<parse error>"
    terms = root.count_terms_linear() if root.is_linear() else -1
    return f"nodes={root.count_nodes(None)} alt={root.compute_alternation(None)} altLin={root.compute_alternation_linear(False)} terms={terms}"


def main():
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    exprs = load_exprs(limit)
    st_ok = st_diff = stats_ok = stats_diff = 0
    for e in exprs:
        cstate = cpp("state", e, 64)
        pstate = py_state(e, 64)
        if cstate == pstate or (cstate.startswith("ERROR") and pstate == "<parse error>"):
            st_ok += 1
        else:
            st_diff += 1
            if st_diff <= 8:
                print(f"[STATE DIFF] {e[:60]!r}")
                print("  cpp:\n" + "\n".join("    " + l for l in cstate.splitlines()[:12]))
                print("  py :\n" + "\n".join("    " + l for l in pstate.splitlines()[:12]))

        cstats = cpp("stats", e, 64)
        pstats = py_stats(e, 64)
        if cstats == pstats or (cstats.startswith("ERROR") and pstats == "<parse error>"):
            stats_ok += 1
        else:
            stats_diff += 1
            if stats_diff <= 8:
                print(f"[STATS DIFF] {e[:60]!r}\n  cpp: {cstats}\n  py : {pstats}")

    print(f"\nstate: {st_ok} ok, {st_diff} diff   stats: {stats_ok} ok, {stats_diff} diff   total {len(exprs)}")
    sys.exit(1 if (st_diff or stats_diff) else 0)


if __name__ == "__main__":
    main()
