# Remaining Work — Plan (2026-09-15)

> Audit of what is left after: Z3 QF_BV speedup (b28ad0b), msimba proving +
> test suites (77098ab), `auto` default + `--auto-fallback` (b2eae11).
> Baselines measured on this machine unless noted.

## 0. Status snapshot

| Area | Status |
|---|---|
| MSiMBA normalizer vs ground truth | **DONE** — 19,000/19,000 (100%) match (`MSIMBA_100_PERCENT_PLAN.md`) |
| GAMBA native port | **DONE** — perf P0/P1 done; P2 optional (`GAMBA_PERF_PLAN.md`) |
| Z3 proving (QF_BV) | **DONE** — 10,000–60,000x on mul-heavy; hard semi-linear still times out (root cause: structure, not width) |
| msimba `--prove` wiring | **DONE** (77098ab) |
| Prove test suite (honest Z3 metric) | **DONE** — 32-bit simba/msimba, 8-bit gamba, 10 s timeout |
| Canonical ground-truth validation | **DONE** — 934/950 canonical, 0 bad, 16 fail (negative-constant coverage gap, see P2) |
| `auto` default + `--auto-fallback` | **DONE** (b2eae11) — fixes checkLinear over-claim mis-routes |
| CoBRA dataset benchmark | **DONE** (2026-09-15) — 99.5% new / 100% already-covered; see section below |
| verify() width fix (lifted.ll) | **DONE** (checklist fully ticked, `REMAINING_WORK_VERIFY_FIX.md`) |

**Prove-suite baseline (N=5, 32-bit, 10 s, honest metric):**
simba 22/85, msimba 24/95, gamba 32/35 — total 78/215.
Full N=100 run: in progress at audit time (log `/tmp/prove_suite.log`); update
this table when it lands. The bulk of the unproved cases are Z3 QF_BV
timeouts on 4+ variable semi-linear expressions (the measured root cause).

---

## CoBRA dataset benchmark (DONE 2026-09-15)

Downloaded `test/datasets` from `github.com/trailofbits/CoBRA` into
`data/CoBRA/` and benchmarked SiMBA++ (auto routing, fast-check) via
`tests/run_cobra_tests.py` (reproducible: `python3 tests/run_cobra_tests.py`).

**Coverage (user's suspicion confirmed):**
- `gamba/*` (7 files) — **content-identical** to our `data/GAMBA/` (CRLF +
  `# Source` headers only). Already covered.
- `simba/e1-e5_*` (16 files) — the ORIGINAL const-free Denuvo variant
  (CoBRA sourced it from the pgarba/SiMBA- repo). We benchmark the
  const-augmented variant (`data/MSiMBA/`, 100% GT match). Both covered.
- **New to us:** `msimba.txt` (1000, semi-linear AND-masked 64-bit),
  `univariate64.txt` / `multivariate64.txt` (1000 each, polynomial),
  `permutation64.txt` (12), `obfuscatorx.txt` (7, from a commercial
  obfuscator, hex consts), `oses_{fast,slow}.txt` (479, OSES
  equal-saturation), `simba/{blast_dataset1,2,pldi_linear,pldi_poly,
  pldi_nonpoly,test_data}.txt` (~17,500).

**Full-run results (fast-checked solve rate, auto routing):**

| Dataset | Solved | Note |
|---|---|---|
| msimba (new, 64-bit) | **999/1000 (99.9%)** | 1 fail = normalizer mask-constant bug (below) |
| univariate64 (new, 16-bit) | **1000/1000** | polynomial class, general route |
| multivariate64 (new, 16-bit) | **1000/1000** | polynomial class, general route |
| permutation64 / obfuscatorx (new) | **13/13, 7/7** | 100% |
| oses_fast (new, 64-bit) | 379/472 (80.3%) | 46 fails + 3 skips = 4-var **nonlinear at 64-bit** (general route is ≤16-bit by design) |
| oses_slow (new, 64-bit) | 5/7 | same class |
| simba/blast1+2, pldi_*, test_data (new) | **16,581/16,581 (100%)** | incl. 10k-line test_data |
| simba/e1_2vars, e5_4vars (cov) | **100%** with **exact GT match** | original variant: our output == dataset GT string |
| gamba qsynth_ea/syntia/neureduce (cov) | **10,500/10,500 (100%)** | canonical form differs from GT string (semantically valid) |

**Totals: NEW 18,984/19,080 (99.5%) · ALREADY COVERED 13,000/13,000 (100%).**

The only unsolved classes are (a) 4-var nonlinear at 64-bit (design limit:
general route ≤16-bit; msimba is semi-linear only) and (b) the normalizer
mask-constant bug below.

### Bug: msimba normalizer rejects specific mask constants (found via CoBRA)

`MultibitSimplifier` fails (returns "") for masked terms `c & x` with
certain constants c — value-specific, at **all bit widths**, all syntactic
forms. Minimal repros (64-bit, `--simplifier=msimba`):

- `(-10&x)` → no result; `(-9&x)` → `-9&x` ✓
- `5 + (1*(-10&x))` → no result; `5 + (1*(5678&x))` ✓
- failing c include: **-10, -16, -24, -32, -56, 2^63, -1863238229756760676**
  (msimba.txt line 1, a full 64-bit bitmask decomposition);
- working c include: -1…-9, -15, -31, -33, 56, 65535, 2^63-1, 2^64-1.
- Telling: `(-10&x) + (-9&x)` **works** (mask+mask path) while the
  single-term and constant+mask paths fail → the bug is in the coefficient
  fitting for the single-variable mask term (`simplifyGeneric` /
  `MultibitRefiner`), not the mask+mask combiner.
- The fast-check verification gate in `MultibitSimplifier::simplify`
  (MultibitSimplifier.cpp ~line 956) catches the wrong intermediate results
  → **no soundness risk**, only a completeness gap (1/1000 on msimba.txt,
  plus the 16 `test_canonical` negative-constant fails are likely the same
  root cause).

**Fix task (½–1 day):** instrument `simplifyGeneric`/`simplifyEntry` for
`(-10&x)` at 8-bit (c=246), find where the fitted coefficient for mask 246
is wrong (suspect: modular-arithmetic signed/unsigned handling in
`subtractCoeff` / `canChangeCoefficientTo` / `tryEliminateUniqueValues`),
fix, re-run `tests/test_canonical.py` + `tests/run_cobra_tests.py`.

---

## P0 — Repair the GAMBA differential test suite (NEW finding — false passes)

**Problem (measured 2026-09-15).** Commit `3d44203` removed the vendored
Python oracle (`external/GAMBA/src/`) — by design, the C++ port is complete.
Consequence: **all 10 differential tests in `MBA/run_all_tests.py` are dead,
and the runner reports them as PASS:**

| Test | Failure mode on Linux | Counted as |
|---|---|---|
| diff_parse, diff_node, diff_refine, diff_simplify, diff_bitwise, diff_parse_dataset, diff_phase4, diff_subst, test_divrem_semantics | crash at import (`ModuleNotFoundError: parse/node/create_bitwise/simplify` — oracle gone) | PASS (false) |
| diff_general | dataset path hardcoded `C:\github\...` → loads 0 expressions → "0 identical, 0 differ" | PASS (false) |
| diff_qsynth_ea | backslash path bug (`data\GAMBA\qsynth_ea.txt` as one filename) | crash → PASS (false) |
| test_tier2_semantics | actually runs, but spawns **Wine + `MBA/build/mba_cli.exe`** per eval (800+ Wine launches at N=100, ~30 min → hits the 600 s per-test timeout → TIMEOUT/FAIL) | FAIL (slow, not broken) |

So "all 9/12 tests PASS" is a **false signal** — the suite currently verifies
nothing.

**Fix (≈ 2–4 h):**
1. `run_all_tests.py`: a child that crashes (non-zero exit / exception) must
   be a **FAIL**, not a silent pass. This alone makes the current state
   honest (suite reports 9–10 FAILs).
2. Delete the 9 dead differential tests (oracle is gone; they served their
   purpose during the port). Keep the scripts in an archive dir
   (`MBA/archive_differential/`) or git history if preferred over deletion.
3. `test_tier2_semantics.py`: use the **native** `build-linux/mba_cli`
   (verified working: `eval 8 "a / 3" 102` → 34) with a fallback chain
   (native binary → `build/mba_cli` → Wine `.exe` as last resort on
   non-Windows). N=100 then runs in seconds instead of ~30 min.
4. Living suite afterwards: `MBA/run_all_tests.py` = tier2_semantics only
   (plus any new native-only semantics tests); the real verification weight
   is in `tests/run_prove_tests.py` + `tests/test_canonical.py`.

**Acceptance:** `python3 MBA/run_all_tests.py` finishes in < 2 min, all
listed tests genuinely executed, exit 0; a deliberately broken child fails
the runner.

---

## P1 — Kissat SAT proving (Phase 3b): independent proofs for the hard semi-linear class

**Why it's the main remaining item.** The honest prove metric is capped at
~36% (N=5: 78/215) because Z3 QF_BV (and Bitwuzla QF_BV) time out on
`sum(const × bitwise)` over 4+ variables at **any** width — measured, not
assumed. The normalizer is validated against ground truth (P0 of
`Z3_PROVE_SEMILINEAR_PLAN.md`), so results are trustworthy, but hard cases
have **no independent proof**. Kissat provides one (complete SAT refutation).

**Plan exists and is de-risked:** `KISSAT_PROVE_PLAN.md` — Tseitin in-house
(~100 lines; Z3 `goal::dimacs()` returns null on bit-blasted let-bindings),
Kissat fetchable (single .h/.c, MIT), CaDiCaL already built as fallback.

**Effort:** ~3–4 days. **Step 0 (½ day) is the gate:** standalone benchmark
bit-blasting `/tmp/conj32.smt2` + `/tmp/conj.smt2`, Tseitin to CNF, Kissat
solve. Proceed only if 32-bit → UNSAT < 1 s and 64-bit → UNSAT < 30 s
(fallback: per-bit instances, trivially parallel).

**Expected effect:** msimba prove metric jumps from 24/95 (N=5) toward
near-complete; simba hard cases improve; the prove suite becomes a real
independent-verification metric instead of "trusted on timeout".

---

## P2 — Normalizer negative-constant / mask-constant gap (small, ½–1 day)

`tests/test_canonical.py`: 16 fails (934 canonical, 0 bad). All are
negative-constant edge cases the normalizer does not handle at all
(`-3335`, `-6*…`, `^` in the ground truth) — a **coverage gap, not a
canonicity problem** (where the normalizer succeeds, it is canonical and
matches ground truth). The CoBRA benchmark (section above) found the same
gap from the other side: specific mask constants (`-10`, `2^63`, …) make
`simplifyGeneric`'s coefficient fitting produce wrong results that the
fast-check gate then rejects. Likely one root cause.

**Fix:** instrument `simplifyGeneric`/`simplifyEntry` for `(-10&x)` at
8-bit (c=246), find the bad fitted coefficient (suspect: modular
arithmetic signed/unsigned handling in `subtractCoeff` /
`canChangeCoefficientTo` / `tryEliminateUniqueValues`), fix, then
re-run `tests/test_canonical.py` (higher N) + `tests/run_cobra_tests.py`.
**Acceptance:** 0 canonical fails at N=100; msimba.txt 1000/1000.

---

## P3 — Optional / lower priority

1. **GAMBA perf P2** (`GAMBA_PERF_PLAN.md`): C1 smarter substitution
   candidates, C2 per-substep time budget, C3 parallel sub-expressions.
   Only if general-route wall time becomes a problem in practice.
2. **Git submodules** for `external/MSiMBA` (330 M), `external/bitwuzla`
   (109 M), `external/yices2` (154 M) — currently untracked separate repos;
   submodules give reproducible pins. Housekeeping, ~1 h.
3. **`--accept-unknown` fast path** — already implemented and documented as
   ad-hoc "good enough" proving (not the reported metric). No action beyond
   what `Z3_PROVE_SEMILINEAR_PLAN.md` Phase 2 records.
4. **lifted.ll shorter entry/continue rewrite** (`SHORTER_ENTRY_CONTINUE_REWRITE.md`)
   — Windows-era target; `llvm/lifted.ll` is not in this workspace. Close as
   N/A unless the file comes back.
5. **Phase 3a/3c (long-term):** e-graph prover (weeks) or proof-by-derivation
   (days–weeks) — only after P1, if proof-by-SAT is insufficient.

---

## Suggested order

1. **P0** (honest test signal) — 2–4 h, unblocks trust in everything else.
2. **P1** (Kissat) — 3–4 days, the main capability gain.
3. **P2** (negative constants) — ½–1 day, can slot between P1 steps.
4. **P3** items as needed.
