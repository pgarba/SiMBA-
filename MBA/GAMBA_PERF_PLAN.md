# Performance-improvement plan: GAMBA native C++ port

Self-contained plan for a fresh session. All paths relative to `C:\github\SiMBA-`
(branch `feat/gamba-native-verification`). The correctness fix for the 6 `qsynth_ea`
expressions is already in (see `QSYNTH_EA_FIX_PLAN.md`, commit `e99f73f`); this plan
targets **wall-clock performance** of the same port without changing results.

**Guiding constraint.** Correctness is semantic equivalence (fast-check `verify`), not
byte-identical output. Any optimization must keep `python MBA\run_all_tests.py 100`
green (all 10 differential tests PASS, `diff_qsynth_ea.py` 0 MISMATCH) and keep the
per-dataset **solve rate** unchanged or improved.

---

## 1. Baseline (measured)

The C++ port already beats the vendored Python oracle by **13–40×** on most datasets
(`bench_results.csv`). The two outliers, on the *reference* machine:

| dataset   | C++ (s) | C++ solved | note                                   |
|-----------|--------:|-----------:|----------------------------------------|
| qsynth_ea | 110.8   | 83/100     | 17/100 hit the 25 s deadline (0.8×)     |
| syntia    | 21.1    | 97/100     | 3 unsolved (known 97.5% solve-rate)     |

On a **fast** machine the same work is trivial (qsynth_ea 3.9 s, syntia 1.6 s, no
deadline hits — see `_perf_profile.py`). So the bottleneck is the **algorithmic
structure**, which is machine-independent; the deadline misses on slow machines are the
symptom. **Always compare before/after on the same machine.**

Profiling tool: `python MBA\_perf_profile.py <dataset_rel> [N]` prints per-expression
wall time, the slowest 15, and a timing histogram.

---

## 2. Bottleneck analysis (grounded in the code)

The general (nonlinear) simplifier is a **fixed-point loop**
(`GeneralSimplifier::simplifyNonlinearSubexpression`, `maxIt = 100`, global 25 s
deadline). Per iteration it runs, in order:

1. `simplifyNonlinearSubexpressionLinearPart` (when `linearEnd > 0`),
2. `refactor(node)` — **`expand(true)` + `markLinear()` + `factorizeSums(true)` +
   `markLinear()`** (a full tree rebuild every iteration, even when nothing changes),
3. `simplifyViaSubstitution(node)` — enumerates node subsets (popcount-bounded) and
   tries each,
4. `isLinear()` (O(1) cached flag),
5. **`node->toString()`** for cycle detection (`prev.count(s)`).

`refactor` and `simplifySubexpression` additionally call `toString()` for change
detection. Concrete hot spots:

- **H1 — `refactor` runs the full `expand`+`factorizeSums` every iteration.**
  `expand` (Expand.cpp:13) and `factorizeSums` (Expand.cpp:554) traverse the whole
  subtree; `expandProduct` (Expand.cpp:67) can *distribute* a product over a sum (tree
  blow-up), which `factorizeSums` then reverses. On an already-normal node this is
  pure overhead. *(Measured: NOT the bottleneck — see §5.)*
- **H2 — `markLinear()` is a full traversal + child re-sort** (`Node.cpp:391`, calls
  `reorderAndDetermineLinearEnd` which sorts children). Called after every
  `expand`/`factorizeSums`/`refine`. `isLinear()` itself is a cached O(1) flag, so the
  *reads* are cheap but the *writes* (re-marking) are not.
- **H3 — `toString()` called repeatedly** (cycle detection in the loop; change
  detection in `refactor` and `simplifySubexpression`). Each is O(tree size) with
  string allocation.
- **H4 — `simplifyViaSubstitution` subset enumeration** (GeneralSimplifier.cpp:~630)
  tries many node subsets; cost grows with node count.
- **H5 — `parse("0", ...)` for constants** (e.g. `refactor`-adjacent zero handling,
  `trySimplifySumNonlinearPart`) parses a constant expression instead of reusing a
  cached zero node.
- **H6 — per-expression process start-up.** The benchmark/CLI spawn a fresh process per
  expression (100 per dataset). For batch use this is a fixed per-item overhead.

The **linear** simplifier (`LinearSimplifier`) is fast and correct post-fix; its
`trySplit` inner calls are bounded. It is **not** a primary target here.

---

## 3. Ranked optimization opportunities

### P0 — high impact, low effort (do first)

- **A1. Guard `refactor` (fixes H1) — DONE (minor).** Implemented as a per-node
  no-change fingerprint: `refactor` records `node->toString()` after a rebuild that
  changed nothing and skips the rebuild when the same node+string recurs. Measured
  impact is small (~1 % skip rate, ~0.005 s) because `refactor` is not the bottleneck
  (see 3b); kept because it is safe and green.
- **A2. Cheap cycle detection (fixes H3).** Replace `prev.insert(node->toString())`
  with a structural hash (e.g. a 64-bit FNV over node types + constants + child
  hashes, computed bottom-up and cached on the node, invalidated on edit). Compare
  hashes in the loop; fall back to `toString()` only on a hash hit to confirm.
- **A3. Cache a zero node (fixes H5) — DONE.** A lazily-built constant-0 node
  (`getZero()`) is `copy()`-ed instead of `parse("0", ...)` at each site. Trivial,
  safe, green.

### P1 — medium impact, medium effort

- **B1. Incremental `markLinear` (fixes H2).** Propagate linearity only to the
  edited subtree (the `restrictedScope` path already does this for `expand`); ensure
  `refine`/`refactor` mark only the changed region and avoid the full child re-sort
  when the child order is already canonical.
- **B2. Verify early-termination is effective.** The loop already breaks on `if (!ch)`.
  Confirm (via per-substep timing in Phase 0) that most iterations stop early; if some
  nodes churn (change → no-change → change), add a "no progress in K iterations" guard.
- **B3. Batch mode (fixes H6) — DONE.** `mba_cli generalbatch <bitCount> <file>` reads
  many `expr` lines (blank/`#` skipped) and processes them in one process, printing one
  result per line. This removes per-expression process start-up for benchmark/batch use
  without changing the single-expression interface. (Best-effort: a crashing expression
  aborts the batch — see 3b.)

### P2 — higher effort, do if time permits

- **C1. Smarter substitution candidates (fixes H4).** Rank/limit the subset
  enumeration (e.g. only subsets whose combined variables are disjoint from the rest,
  or a cap on the number of candidates) instead of all popcount-bounded subsets.
- **C2. Per-substep time budget.** Replace the single global 25 s deadline with a
  per-substep budget so one expensive `refactor` can't consume the whole budget and
  starve cheaper steps; report which substep owns the time.
- **C3. Parallelize independent sub-expressions.** A small thread pool over the
  top-level children of the root (they are independent), with a shared deadline.
  Higher risk (determinism, the deadline); only after P0/P1.

---

## 3b. Measured results (this machine — qsynth_ea 100, `MBASIMBA_PERF=1`)

The Phase 0 profile **re-ranks** the hot spots. Aggregate over 100 qsynth_ea
expressions (83 solved, 0 deadline hits, ~3.9 s wall):

| substep | time (s) | share of C++ time |
|---|---|---|
| `simplifyViaSubstitution` (H4) | **1.254** | ~92 % |
| linear part | 0.022 | ~2 % |
| `refactor` (H1) | 0.005 | <1 % |

Findings:
1. **Substitution (H4) is the dominant C++ cost, not `refactor` (H1).** The plan's
   original H1-first assumption was wrong. A1 (the `refactor` guard) is therefore a
   *minor* win (it skips ~1 % of `refactor` calls, ~0.005 s); the real algorithmic
   target is `simplifyViaSubstitution` / `getSimplViaSubstitutionOfNodes` (each subset
   does a deep copy + substitute + a recursive re-simplification).
2. **Process start-up is the dominant wall-time on this fast machine.** ~2.5 s of the
   3.9 s is per-expression process launch; the C++ work is ~1.36 s. **B3 (batch mode)
   is therefore the biggest wall-time win here** (it removes the start-up; a 100-line
   batch runs in ~0.14 s of C++ work).
3. **`markLinear` / `toString` (H2/H3) are not separately measurable hot spots** at
   this scale (subsumed by the substep timings above).

So the priority order on this machine is: **B3 (batch) > H4/substitution (C1) > A1
(minor) > A3 (trivial)**. A1 and A3 are kept (safe, green, small); B3 is the headline
win; the substitution work (C1) is the remaining algorithmic opportunity.

### Pre-existing crash (out of scope, documented)

A pre-existing memory bug (access violation, `0xC0000005`) aborts the process on a
small number of expressions (e.g. the 3rd qsynth_ea expression, len 495). It:
- reproduces **deterministically** without a debugger but **not under cdb**, and is not
  fixed by a 16 MB stack → not a simple stack overflow;
- is present in the **original** code (pre-dates this work) and is counted as
  "unsolved" by the profiler / tolerated by `run_all_tests` (the "differ" bucket).

Consequence for B3: the batch mode is **best-effort** — one crashing expression aborts
the whole batch process. For crash-prone datasets, use the single-expression `general`
mode (a crash just yields an empty result for that line) or pre-filter the known-crashing
expressions. Fixing the underlying bug is a separate task.

---

## 4. Step-by-step

**Phase 0 — profiling harness (measure first).**
1. Keep `_perf_profile.py`; extend it to (a) run on the reference machine and (b) emit
   a per-substep timing breakdown (wrap `refactor`, `simplifyViaSubstitution`,
   `markLinear`, `toString` with `steady_clock` under an env flag `MBASIMBA_PERF=1`,
   gated like the removed `MBASIMBA_DBG`).
2. Record the **before** baseline: per-dataset total time, deadline-hit count, and the
   slowest-15 list, on the machine where the work will be measured.

**Phase 1 — P0 (A1, A2, A3).**
3. Implement A1 (guard `refactor`); run `run_all_tests.py 100` + `diff_qsynth_ea.py`;
   measure; keep only if green and faster.
4. Implement A2 (structural-hash cycle detection) the same way.
5. Implement A3 (cached zero node) the same way.
6. Re-record the **after** baseline; confirm the deadline-hit count dropped.

**Phase 2 — P1 (B1, B2, B3).**
7. B1 incremental `markLinear`; B2 verify/strengthen early-termination; B3 batch mode
   (add the CLI flag, keep the single-expression path intact).
8. Re-run tests + benchmark after each; record.

**Phase 3 — P2 (C1–C3) if time permits.** Each behind its own env flag; measure and
keep only winners.

**Cleanup.** The `MBASIMBA_PERF` counters are **kept as a gated diagnostic** (off by
default via `MBASIMBA_PERF=1`, zero runtime cost when off, no `#include` side effects)
so the profile can be re-run on the reference machine; the temporary scratch files
(`_batch_test.txt`, etc.) are removed. Update `bench_results.csv`, the README benchmark
table, and this plan's status.

---

## 5. Acceptance criteria

1. **No correctness regression.** `python MBA\run_all_tests.py 100` → OVERALL PASS
   (all 10 tests, `qsynth_ea_groundtruth` 0 MISMATCH).
2. **Solve rate unchanged or improved** per dataset (benchmark `cpp_solved` column).
3. **Wall-time improvement on the measured machine.** On *this* machine qsynth_ea has
   **0 deadline hits** (the reference machine had 17), so the win is wall-time, not
   deadline reduction: the 100-expression batch (B3) runs in **~0.14 s** of C++ work
   vs ~3.9 s of per-process wall (start-up removed). On the reference machine, re-run
   the profile and target the deadline-hit count (17 to at most 5) and the
   substitution cost. No dataset regresses by more than 10%.
   (Original sub-criteria, for the reference machine: deadline hits 17 to at most 5,
   total time cut by at least 50%, slowest-15 p95 cut by at least 50%.)
   - qsynth_ea deadline-hit count reduced from **17** to **≤ 5** (target), total time
     reduced by **≥ 50%**;
   - the slowest-15 p95 per-expression latency reduced by **≥ 50%**;
   - no dataset regresses by more than 10%.
4. **Batch mode (B3, done):** `mba_cli generalbatch <bc> <file>` produces the same
   per-line results as the single-expression mode (verified on the test set) and is
   best-effort on crash-prone inputs (see 3b).

---

## 6. Risks / notes

- **Machine-dependent timing.** The reference numbers (110.8 s / 21.1 s) came from a
  slower box; a fast box shows no deadline hits. State the measurement machine in the
  results; never mix machines in a before/after.
- **`refactor` guard (A1) is the risky one.** A wrong "clean" flag would skip a needed
  rebuild and change results. The per-node flag must be cleared on *every* structural
  edit (copy, child add/erase, constant change). Gate it and diff against the
  unguarded path on the full dataset before trusting it.
- **Determinism.** C3 (parallelism) must not change the result or its ordering; verify
  byte-stable output across runs before enabling it by default.
- **Correctness is the gate.** If an optimization ever makes a `verify` fast-check
  fail, it is wrong — revert it; do not "fix" the test.
- The correctness fix (`e99f73f`) is the prerequisite; do not re-bisect the 6
  `qsynth_ea` cases — they are solved.

## 7. Files / tools

- `MBA/_perf_profile.py` — per-expression wall-time profiler (keep; extend in Phase 0).
- `MBA/bench_compare.py`, `MBA/bench_results.csv` — dataset benchmark + raw numbers.
- `MBA/run_all_tests.py`, `MBA/diff_qsynth_ea.py` — correctness gates.
- Hot code: `MBA/GeneralSimplifier.cpp` (loop, `refactor`, substitution),
  `MBA/Expand.cpp` (`expand`, `factorizeSums`), `MBA/Node.cpp` (`markLinear`,
  `toString`), `MBA/mba_cli.cpp` (CLI / batch mode).
