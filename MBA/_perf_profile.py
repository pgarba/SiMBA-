#!/usr/bin/env python3
"""Profile per-expression C++ `general` timing on a dataset.
Usage: python _perf_profile.py <dataset_rel> [N]
Prints the slowest expressions (wall time) and aggregate stats.
"""
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
_EXE = os.path.join(ROOT, "build-linux", "mba_cli")
EXE = _EXE if os.path.isfile(_EXE) else os.path.join(HERE, "build", "mba_cli.exe")
BC = 8

PERF_PAT = re.compile(
    r"iters=(\d+) refactor=(\d+) skips=(\d+) tLinear=([\d.]+) "
    r"tRefactor=([\d.]+) tSubst=([\d.]+) tTotal=([\d.]+)")
PERF_SUBST_PAT = re.compile(
    r"substCalls=(\d+) subsetTried=(\d+) substFound=(\d+) substImproved=(\d+) "
    r"substWalkChanged=(\d+) "
    r"triedByPop=\[(\d+),(\d+),(\d+),(\d+)\] impByPop=\[(\d+),(\d+),(\d+),(\d+)\] "
    r"nodesHist=\[(\d+),(\d+),(\d+),(\d+)\] "
    r"loopNoChange=(\d+) loopCycle=(\d+) loopDeadline=(\d+) loopMaxIt=(\d+) "
    r"tSubCopy=([\d.]+) tSubRefine=([\d.]+) tSubSimplify=([\d.]+) "
    r"tSubMech=([\d.]+) tSubTail=([\d.]+) tSubAccept=([\d.]+) tSubAccRefine=([\d.]+) "
    r"tSubAccMarkLin=([\d.]+) tComplexity=([\d.]+) "
    r"tLinearSub=([\d.]+) tLinearMba=([\d.]+) tLinearParse=([\d.]+) "
    r"linearSubCalls=(\d+) linearSubChanged=(\d+) linearSubDupCalls=(\d+) "
    r"linearCacheHits=(\d+) "
    r"toStringCalls=(\d+) toStringT=([\d.]+) "
    r"checkT=([\d.,]+) "
    r"checkFired=([\d,]+)")


def load(path, n):
    seen, out = set(), []
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            expr = line.split(",")[0].strip()
            if expr and expr not in seen:
                seen.add(expr)
                out.append(expr)
            if len(out) >= n:
                break
    return out


def main():
    args = [a for a in sys.argv[1:] if a != "--perf"]
    perf = "--perf" in sys.argv
    rel = args[0]
    n = int(args[1]) if len(args) > 1 else 100
    exprs = load(os.path.join(ROOT, rel), n)
    env = dict(os.environ)
    if perf:
        env["MBASIMBA_PERF"] = "1"
    print(f"Profiling {len(exprs)} expressions from {rel} ({BC}-bit) perf={perf}")
    rows = []
    agg = dict(iters=0, refactor=0, skips=0, tLinear=0.0, tRefactor=0.0,
               tSubst=0.0, tTotal=0.0)
    agg_subst = dict(calls=0, tried=0, found=0, improved=0, walkChanged=0,
                     triedByPop=[0, 0, 0, 0], impByPop=[0, 0, 0, 0],
                     nodesHist=[0, 0, 0, 0], loop=[0, 0, 0, 0],
                     tSubCopy=0.0, tSubRefine=0.0, tSubSimplify=0.0,
                     tSubMech=0.0, tSubTail=0.0, tSubAccept=0.0,
                     tSubAccRefine=0.0, tSubAccMarkLin=0.0, tComplexity=0.0,
                     checkT=[0.0] * 13, checkFired=[0] * 13,
                     tLinearSub=0.0, tLinearMba=0.0, tLinearParse=0.0,
                     linearSubCalls=0, linearSubChanged=0, linearSubDupCalls=0,
                     linearCacheHits=0, toStringCalls=0, toStringT=0.0)
    for k, e in enumerate(exprs):
        t0 = time.perf_counter()
        r = subprocess.run([EXE, "general", str(BC), e],
                           capture_output=True, text=True, timeout=30, env=env)
        dt = time.perf_counter() - t0
        solved = bool(r.stdout.strip())
        rows.append((dt, k, solved, e))
        if perf:
            m = PERF_PAT.search(r.stderr)
            if m:
                agg["iters"] += int(m.group(1))
                agg["refactor"] += int(m.group(2))
                agg["skips"] += int(m.group(3))
                agg["tLinear"] += float(m.group(4))
                agg["tRefactor"] += float(m.group(5))
                agg["tSubst"] += float(m.group(6))
                agg["tTotal"] += float(m.group(7))
            ms = PERF_SUBST_PAT.search(r.stderr)
            if ms:
                agg_subst["calls"] += int(ms.group(1))
                agg_subst["tried"] += int(ms.group(2))
                agg_subst["found"] += int(ms.group(3))
                agg_subst["improved"] += int(ms.group(4))
                agg_subst["walkChanged"] += int(ms.group(5))
                for i in range(4):
                    agg_subst["triedByPop"][i] += int(ms.group(6 + i))
                    agg_subst["impByPop"][i] += int(ms.group(10 + i))
                    agg_subst["nodesHist"][i] += int(ms.group(14 + i))
                    agg_subst["loop"][i] += int(ms.group(18 + i))
                for g, k in ((22, "tSubCopy"), (23, "tSubRefine"), (24, "tSubSimplify"),
                             (25, "tSubMech"), (26, "tSubTail"), (27, "tSubAccept"),
                             (28, "tSubAccRefine"), (29, "tSubAccMarkLin"),
                             (30, "tComplexity"), (31, "tLinearSub"), (32, "tLinearMba"),
                             (33, "tLinearParse"), (39, "toStringT")):
                    agg_subst[k] += float(ms.group(g))
                agg_subst["linearSubCalls"] += int(ms.group(34))
                agg_subst["linearSubChanged"] += int(ms.group(35))
                agg_subst["linearSubDupCalls"] += int(ms.group(36))
                agg_subst["linearCacheHits"] += int(ms.group(37))
                agg_subst["toStringCalls"] += int(ms.group(38))
                for i, tv in enumerate(
                        float(x) for x in ms.group(40).split(",")):
                    agg_subst["checkT"][i] += tv
                for i, fv in enumerate(int(x) for x in ms.group(41).split(",")):
                    agg_subst["checkFired"][i] += fv
    if perf:
        print("--- PERF aggregate ---")
        print(f"  iters={agg['iters']}  refactor={agg['refactor']}  "
              f"skips={agg['skips']}  skipRate={agg['skips'] / max(1, agg['refactor']):.2f}")
        print(f"  tLinear={agg['tLinear']:.3f}  tRefactor={agg['tRefactor']:.3f}  "
              f"tSubst={agg['tSubst']:.3f}  tTotal={agg['tTotal']:.3f}")
        print(f"  substCalls={agg_subst['calls']}  subsetTried={agg_subst['tried']}  "
              f"found={agg_subst['found']}  improved={agg_subst['improved']}  "
              f"walkChanged={agg_subst['walkChanged']}")
        print(f"  triedByPop(1-4)={agg_subst['triedByPop']}  "
              f"impByPop(1-4)={agg_subst['impByPop']}")
        print(f"  nodesHist(<=4,5-9,10-20,>20)={agg_subst['nodesHist']}")
        print(f"  loop terminations (noChange,cycle,deadline,maxIt)={agg_subst['loop']}")
        print(f"  per-attempt: tSubCopy={agg_subst['tSubCopy']:.3f}  "
              f"tSubRefine={agg_subst['tSubRefine']:.3f}  "
              f"tSubSimplify={agg_subst['tSubSimplify']:.3f}  "
              f"tSubMech={agg_subst['tSubMech']:.3f}  "
              f"tSubTail={agg_subst['tSubTail']:.3f}  "
              f"tSubAccCopy={agg_subst['tSubAccRefine']:.3f}  "
              f"tSubAccRefineMarkLin={agg_subst['tSubAccMarkLin']:.3f}  "
              f"tComplexity={agg_subst['tComplexity']:.3f}")
        print(f"  linearSub: calls={agg_subst['linearSubCalls']}  "
              f"changed={agg_subst['linearSubChanged']}  "
              f"dupInputs={agg_subst['linearSubDupCalls']}  "
              f"total={agg_subst['tLinearSub']:.3f}  "
              f"solver={agg_subst['tLinearMba']:.3f}  "
              f"parse={agg_subst['tLinearParse']:.3f}  "
              f"cacheHits={agg_subst['linearCacheHits']}")
        names = ["sumsCancel", "sumsReplace", "disjXorSums", "xorDisj",
                 "negBitwInv", "xorPairsC", "bitwPairsC", "diffBitwC",
                 "bitwTuplesC", "bitwPairsI", "diffBitwI", "bitwAndSum",
                 "insXorSum"]
        print("  pattern checks (time, fired):")
        for i, nm in enumerate(names):
            ct = agg_subst["checkT"][i]
            if ct > 0.0005 or agg_subst["checkFired"][i]:
                print(f"    {i:2d} {nm:<12s} t={ct:.4f}  fired={agg_subst['checkFired'][i]}")
        print(f"  toString: calls={agg_subst['toStringCalls']}  "
              f"time={agg_subst['toStringT']:.3f}s")
    rows.sort(reverse=True)
    total = sum(x[0] for x in rows)
    solved = sum(1 for x in rows if x[1 + 1])
    over = sum(1 for x in rows if x[0] > 25)
    print(f"total={total:.1f}s  solved={solved}/{len(rows)}  >25s(deadline)={over}")
    print(f"--- slowest 15 ---")
    for dt, k, solved, e in rows[:15]:
        print(f"  [{k}] {dt:7.2f}s  solved={int(solved)}  len={len(e):4d}  {e[:70]}")
    print(f"--- timing histogram (s) ---")
    buckets = [0.1, 0.5, 1, 2, 5, 10, 25]
    prev = 0
    for b in buckets:
        c = sum(1 for x in rows if prev <= x[0] < b)
        print(f"  {prev:>5.1f}-{b:<5.1f}s : {c}")
        prev = b
    c = sum(1 for x in rows if x[0] >= 25)
    print(f"  >=25.0s   : {c}")


if __name__ == "__main__":
    main()
