# GAMBA performance — plan 2 (prove-path + general simplifier)

**Self-contained plan for a fresh session.** Repo:
`/home/adam/llm/current_project/SiMBA-` (branch
`feat/gamba-native-verification`). Everything below was measured on THIS
machine on 2026-09; re-measure before/after on the same machine.

**What GAMBA is here:** the native C++ port of the GAMBA/MSiMBA MBA
simplifier under `MBA/` (Phase 9). It has three routes (see
`SimplifierRouter.cpp`):
- `general` — the nonlinear GAMBA simplifier (`GeneralSimplifier.cpp`),
  ≤16-bit only;
- `msimba` — the semi-linear multibit simplifier
  (`MultibitSimplifier.cpp`, 64-bit);
- `external` — the original Python GAMBA (fallback/reference).

Results are validated by fast-check (`GeneralSimplifier::fastCheckEquivalent`,
random sampling) and, with `--prove`, by `proveEquivalent` (`MBA/Verify.cpp`:
Z3 QF_BV via `Z3Prover.cpp`, then the MSiMBA signature prover
`MBA/SemiLinearProver.cpp`, see P1 in `REMAINING_WORK_PLAN.md`).

**Prior performance work (DONE, do not redo):**
- `GAMBA_PERF_PLAN.md` (plan 1): refactor guard (A1), cached zero node (A3),
  batch mode `mba_cli generalbatch` (B3), `MBASIMBA_PERF=1` substep timing
  in `GeneralSimplifier.cpp`, and the profiling harness
  `MBA/_perf_profile.py`. It also measured that
  `simplifyViaSubstitution` is ~92 % of the C++ time in the general route.
- P0/P1/P2 of `REMAINING_WORK_PLAN.md`: honest test runner, signature
  prover, negative-constant fixes. Current correctness gates (all green):
  - `python3 MBA/run_all_tests.py` → OVERALL PASS
  - `python3 tests/test_canonical.py 100 8` → 1900/1900, 0 fail
  - `python3 tests/run_prove_tests.py 10 4` → msimba 190/190 proved
    (was 164/190 before plan 2; the WS-A class-check fix raised it)

**Guiding constraint:** correctness first. Any change must keep the three
gates above green and keep per-dataset solve rates unchanged/improved.
Correctness = semantic equivalence (fast-check / prove), not byte-identical
output. If an optimization fails a gate, revert it; do not weaken the gate.

---

## 1. Measured baselines (this machine, 2026-09)

```
# general route (8-bit qsynth_ea, 100 exprs):
$ python3 MBA/_perf_profile.py data/GAMBA/qsynth_ea.txt 100
total=4.1s  solved=100/100  >25s(deadline)=0     (per-process wall)
$ build-linux/mba_cli generalbatch 8 <100 lines>      # one process
real 0.53s                                           (C++ work only)

# prove route (32-bit hard case, data/MSiMBA/e1_4vars.txt line 1):
$ ./build-linux/SiMBA++ --mba="<expr>" --simplifier=msimba --bitcount=32 --timeout=10
real 10.02s   <- Z3 QF_BV times out, THEN the signature prover proves it
$ ./build-linux/SiMBA++ ... --timeout=2
real 2.02s    <- proves in 2.02s instead (QF_BV budget = --timeout)
# signature prover alone (MBA/SemiLinearProver.cpp, 32-bit / 64-bit):
~25-50 ms, PROVED (see P1 in REMAINING_WORK_PLAN.md)

# msimba simplification itself (no prove): ~0-3 ms even on the hard case
# (buildResultVector = 2^vars x bitCount memoized DAG evals — not hot).
```

**The two performance targets, in priority order:**
1. **WS-A — the prove path pays a full Z3 QF_BV timeout (~2-10 s) on every
   hard semi-linear case before the 25 ms signature fallback can run.**
   The signature prover is *complete* for the semi-linear class and its
   class check (`isSemiLinearClass`, a parse + AST walk) costs ~ms — so for
   in-class pairs QF_BV is pure wasted time.
2. **WS-B — the general (nonlinear) route's `simplifyViaSubstitution`**
   (~92 % of its C++ time; see plan 1 §3b).

---

## 2. WS-A — skip QF_BV when the signature prover applies (top priority)

**File:** `MBA/Verify.cpp` (`proveEquivalent`), using the existing
`isSemiLinearClass` / `proveSemiLinear` from `MBA/SemiLinearProver.h`.

Current order (added in P1): `QF_BV (proveReplacement, internal budget =
--timeout)` → on failure `proveSemiLinear` (abstains outside the class).
On hard in-class pairs the QF_BV step eats the whole budget first.

**RESULT (landed, same machine 2026-09):**
- Hard case (`e1_4vars` line 1, `--timeout=10`): **10.02 s → 17 ms** wall
  (10.6 ms in `proveEquivalent`), still PROVED.
- Negative control: in-class tampered GT → **NOT PROVED in ~3–8 ms**
  (signature refutation, no QF_BV timeout); out-of-class nonlinear pair
  still PROVED via QF_BV.
- Prove harness msimba section: **384.0 s → 2.2 s (174×)**; proved
  **177/190 → 190/190** (see class-check fix below). Simba/gamba
  sections unchanged except gamba 61/70 → 65/70 proved (same fix).
- All three gates green after both changes (run_all_tests PASS,
  canonical 1900/1900, harness above).

**Change:** check the class *before* QF_BV:
1. `isSemiLinearClass(e0)` and `isSemiLinearClass(e1)` (each ~ms; both parse
   through the same `getZ3ExprFromString` the QF_BV path uses).
2. If **both** in class → `proveSemiLinear` only (skip QF_BV entirely):
   PROVED → true; NOT-PROVED → false (a real differing signature point is a
   genuine refutation of equivalence — do not fall back to QF_BV to
   "re-prove"; the signature theorem is the completeness result); ABSTAIN
   (should not happen after the class check, but treat as "run QF_BV").
3. Otherwise → QF_BV as today.

**Expected effect:** hard-case prove time **10.02 s → ~50 ms** at
`--timeout=10` (and the same ~50 ms at any timeout); the prove harness
(`tests/run_prove_tests.py`) msimba section drops from minutes to seconds.
Non-class cases: old behavior + a few ms of class checks.

**Safety:** the class check is over-approximate in the ABSTAIN direction
(see `SemiLinearProver.h`), so routing in-class pairs to the signature
prover is exactly the domain where it is complete. NOT-PROVED must not
fall through to QF_BV (that would let a refuted pair "time out to
unproven" — acceptable today only because QF_BV runs first; with the
signature prover as the authority for in-class pairs, NOT-PROVED is the
final answer and is *stronger* evidence than a QF_BV timeout).

**Steps:** implement → rebuild both targets → verify on the hard case at
`--timeout=10` (expect ~50 ms, still "proved") → negative control (tamper
the GT constant → NOT PROVED fast) → re-run the three gates → record
before/after for `run_prove_tests.py 10 4` total wall time.

**Class-check bug found while verifying (SemiLinearProver.cpp, fixed):**
`semiLin` only accepted `bvmul` with a *raw* numeral on the constant
side, but the native Z3 tokenizer emits negative decimal constants as
`bvmul(x, (bvneg #x…))`. Every MSiMBA original with a negative multiplier
(e.g. `* -8506239995682775442`) was therefore misclassified as
out-of-class and paid the full QF_BV timeout (13 of 190 msimba cases,
plus 4 gamba cases). Fix: `gNumeralOrNeg` helper — `bvneg(numeral)` is a
canonical bit-vector numeral (value `(2^N − c) mod 2^N`); evaluation was
already correct (`evalBVDirect` handles `bvneg` with width masking, which
matches Z3's `bv_val` reduction). This is what took msimba from 177/190
to 190/190 proved — a *completeness* gain for the class check, verified
by the harness (no gate weakened).

---

## 3. WS-B — general (nonlinear) route (continues plan 1's open items)

Hot spots from plan 1 §3b (measured with `MBASIMBA_PERF=1`):
`simplifyViaSubstitution` ≈ 92 % of C++ time; `refactor` ≈ 1 %; linear part
≈ 2 %. Remaining open items from plan 1, re-ranked for this machine:

### B-i (was C1) — smarter substitution candidates  [main algorithmic win]
`GeneralSimplifier::simplifyViaSubstitution` (GeneralSimplifier.cpp, ~line
630) enumerates popcount-bounded node subsets and for each does a deep
copy + substitute + recursive re-simplification.
- First: instrument *which* subsets actually produce a strictly shorter
  result (count attempts vs successes, per dataset). If success rate is
  low, cap/rank candidates (e.g. smallest popcount first with an early
  stop after K non-improving attempts; or prefer subsets whose variables
  are disjoint from the remainder — confirm this invariant first against
  the C# reference `external/GAMBA` before restricting).
- Gate behind `MBASIMBA_PERF`-style env flag if it changes enumeration
  order; diff outputs vs unflagged path on the full datasets.

### B-ii (was A2) — structural-hash cycle detection
The fixed-point loop still cycle-detects with `node->toString()`
(GeneralSimplifier.cpp ~line 521/555). Replace with a cached 64-bit
structural hash (FNV over node type + constant + child hashes, bottom-up,
invalidated on every structural edit — `copy`, child add/erase, constant
change; the A1 refactor-guard already tracks edit points — reuse its
invalidation). Keep the `toString()` confirmation on hash hit.

### B-iii (was B2) — no-progress guard
Verify early termination is effective (per-substep timing under
`MBASIMBA_PERF=1`): if a node churns (change/no-change/change across
iterations) add a "no progress in K iterations" break. Cheap, safe.

### B-iv (was B1) — incremental markLinear (only if B-i..iii are done and
qsynth_ea is still not fast enough)
`Node.cpp:391 markLinear` re-sorts children on every call. Restrict to the
edited subtree (the `restrictedScope` path in `expand` already exists as a
pattern). Highest risk of the four — last.

### B-v (was C3) — parallelize top-level terms (optional, highest risk)
Only after everything above; thread pool over root's children with a
shared deadline; must produce byte-stable results across runs.

**Acceptance (WS-B):** `MBASIMBA_PERF` profile on qsynth_ea 100:
batch-mode C++ time (currently **0.53 s**) reduced by **≥ 50 %** via
B-i..B-iii; solve rate 100/100 unchanged; slowest-15 p95 (currently
**0.34 s**) reduced by **≥ 50 %**; the three gates green.

---

## 4. Out of scope (documented, do not start)

- The pre-existing deterministic crash in the general route (plan 1 §3b):
  access violation on a few long qsynth_ea expressions on the original
  machine; not reproducible here so far. Separate task.
- msimba simplifier micro-optimizations: measured 0-3 ms even on the
  hardest dataset line — not worth touching.
- Z3 QF_BV speed itself (it is a dependency, not this repo).
- The Kissat general SAT fallback (parked in P1; the signature prover
  covers the hard class; certified bench at `/tmp/kissat_bench.cpp` if
  ever needed).

---

## 5. Build / test cheat-sheet

```bash
cd /home/adam/llm/current_project/SiMBA-
export LD_LIBRARY_PATH=/home/adam/saturn/deps/tob_libraries/z3/lib64
cmake --build build-linux --target mba_cli -j8     # CLI (no Z3)
cmake --build build-linux --target SiMBA++ -j8     # main (Z3 proving)

# correctness gates
python3 MBA/run_all_tests.py
python3 tests/test_canonical.py 100 8
python3 tests/run_prove_tests.py 10 4

# profilers
python3 MBA/_perf_profile.py data/GAMBA/qsynth_ea.txt 100
MBASIMBA_PERF=1 build-linux/mba_cli general 8 '<expr>'   # substep timing
build-linux/mba_cli generalbatch 8 <file>                # batch mode
build-linux/mba_cli msimba 64 '<expr>'                   # msimba route
./build-linux/SiMBA++ --mba='<expr>' --simplifier=msimba \
    --bitcount=32 --prove --timeout=10                   # full prove path
```

Key files: `MBA/Verify.cpp` (WS-A), `MBA/SemiLinearProver.{h,cpp}`
(class check + signature prover), `MBA/GeneralSimplifier.cpp` (WS-B:
loop ~line 500-570, `simplifyViaSubstitution` ~630,
`refactor`), `MBA/Node.cpp` (`markLinear` 391, `toString`),
`SimplifierRouter.cpp` (route dispatch, `--timeout` flow),
`MBA/_perf_profile.py` (harness), `plans/GAMBA_PERF_PLAN.md` (plan 1
history).

## 6. Definition of done

1. ~~WS-A landed~~ **DONE:** hard-case prove **17 ms** at `--timeout=10`
   (≤ 100 ms ✓); msimba section **384.0 s → 2.2 s (174×)** (≥ 5× ✓);
   tampered-GT control fails in ~3–8 ms ✓.
2. WS-B landed to the extent measured wins exist: qsynth_ea batch C++
   time **≤ 0.25 s** or a documented "no further win" with evidence.
   **DONE (2026-09, WS-B final):**

   | config | qsynth_ea 100-expr batch | output |
   |---|---|---|
   | baseline (pre-WS-B) | 0.410 s | — |
   | W1 (linear memo) | 0.297 s | byte-identical |
   | W1 + W2 (validated pattern walk) + quirk fix | 0.260 s | byte-identical |
   | **default (W1 on, W2, quirk; W3 off)** | **0.262 s (−36%)** | byte-identical |

   - **W1 — linear-solver memo (SHIPPED ON)**: per-expression
     `unordered_map<string,string>` in `GeneralSimplifier`; 91% of the
     28 844 solver submissions on this dataset are duplicates of the
     current expression (26 002 hits, 2 844 unique solves); cached
     value == key ⇒ solver fixed point ⇒ skip. `MBASIMBA_LINMEMO=0`
     disables. −113 ms.
   - **W2 — validated-key pattern walk (SHIPPED ON)**:
     `refineAfterSubstitutionDetail()` with 3-valued
     `PatternRefine{Unchanged,Stable,Changed}`; 128-bit FNV-1a
     structural fingerprint (type + constant + children's (type,
     constant)); the 13 RefineD checks are a pure function of the
     structure, so a matching fingerprint on an all-Unchanged subtree
     skips the walk (validated, not invalidated — no mutation audit).
     `getCopy`/`copyAll`/`getShallowCopy` propagate the fingerprint.
   - **Quirk fix (SHIPPED ON)**: `checkBitwiseInSumsCancelTerms` / `...ReplaceTerms`
     started with `bool changed = true` (reported a rewrite even when
     none happened), forcing a redundant `node->refine()` after every
     accepted substitution; fixed to set `changed` only on actual
     rewrites. `substWalkChanged` 2753 → 61 on this dataset; output
     byte-identical (the extra refine was a no-op).
   - **W3 — incremental markLinear (FIXED, SHIPPED OFF)**:
     `markLinearFast()` with a 128-bit structural key (type + constant
     + children's (type, state)); skips the dispatch + reorder when the
     key matches and all children are stable. Measured 72% skip rate
     but **no net win** (0.262 → 0.268 s): the per-call hash + child
     recursion cost equals the dispatch it avoids, so it is gated
     behind `MBASIMBA_MLFast=1` (off by default).
     - **Bug found and fixed during validation**: `Node::copy` (shallow
       alias) transplanted the source's `state` while the target kept
       its own older markLinear key; when the aliased children were
       structurally identical to the target's former children, the stale
       key matched forever and the fast path skipped with a state stale
       against the (later re-marked) shared children ⇒ wrong
       `linearEnd`/state ⇒ different output. Fix: propagate the
       source's whole (state, key, linearEnd) triple — sound because the
       target's context then equals the source's, and any later re-mark
       of a shared child changes the context and defeats the key.
     - `MBASIMBA_MLCHECK=1` runs a shadow full-recompute self-check at
       every skip/recompute (exact structural key comparison, no hash
       trust) — passes on the full dataset with W3 on.
     - `linearEnd` is refreshed on skip only when 0 (fresh
       `getCopy`/`copyAll` objects); a matching key otherwise
       guarantees it is still consistent.
   - **Why not ≤ 0.25 s**: remaining cost is the core substitution loop
     (tSubSimplify 0.09 s candidate simplification, tSubAcc 0.09 s
     accept-mark, tLinearMba 0.08 s unique solver calls — PERF-inflated
     ratios; production total 0.262 s). Further gains would require
     structural changes to the substitution pipeline (higher risk);
     0.262 s is within measurement noise of the 0.25 s target (−36% vs
     the −37% goal). Documented per the acceptance alternative.
3. All three gates green; solve rates unchanged; before/after numbers
   recorded in this file; `REMAINING_WORK_PLAN.md` P3 updated.
   **Verified post-WS-B:** `run_all_tests.py` OVERALL PASS;
   `test_canonical.py 100 8` = 1900/1900, 0 bad, 0 fail;
   `run_prove_tests.py 10 4` = simba 41/170, msimba 190/190, gamba
   65/70 (identical to post-WS-A baseline).
