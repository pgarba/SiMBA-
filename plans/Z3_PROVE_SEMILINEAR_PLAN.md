# Z3 Proving of Semi-Linear MBAs with Large Constants — Plan

## Problem

Running the prove tests (`tests/run_prove_tests.py`) over the three datasets
(msimba / simba native / gamba) with `--prove`, Z3 proving is **super slow on
expressions with large i64 constants** (the MSiMBA dataset) and times out at
the 30 s deadline.

Measured suite run (N=30/file, 64-bit, 30 s Z3 timeout):

| dataset | proved | note |
|---|---|---|
| simba native (64-bit) | 104/510 | 2-var files mostly prove; 4+-var files 0/30 |
| msimba (64-bit) | 117/570 | same pattern: 2-var OK, 4+-var all time out |
| gamba (8-bit) | 177/210 | fine at 8-bit |

## Root cause (measured: not the width, not the constants)

The failing shape is the **semi-linear** conjecture:

```
sum_i c_i * f_i(vars)  ==  simple_target        (e.g. x + y)
```

where `c_i` are (large) constants and `f_i` are bitwise functions of the
variables. Isolation experiments (benchmarks in `/tmp/z3_*_bench.cpp`, Z3
build at `saturn/deps/tob_libraries/z3`):

1. **Width is not the cause.** The same 4-var expression times out at 64, 32
   *and* 16 bits (30 s+ each).
2. **Constant magnitude is not the cause.** Replacing all large constants with
   `c mod 2^16` still times out at 16 bits.
3. **Z3's own `simplify` does not help** (14 ms to simplify, then QF_BV still
   times out).
4. **Bit-blast + Z3's internal SAT also times out** (30 s+ on the 32-bit
   formula).
5. **Decomposition does not help:** replacing each `c_i * f_i` term by a fresh
   constant `F_i`, asserting `F_i = f_i` (pure bitwise) and proving
   `sum c_i * F_i == target` (pure linear) — still times out. Z3's QF_BV
   normalizer cannot connect the two parts.
6. **2-variable cases prove in milliseconds** at 64-bit. The trigger is the
   number of variables/terms interacting through the multiplications
   (4+ vars, ~8-16 terms).

Conclusion: this Z3 build's word-level QF_BV solver has a hard limit on
`const × bitwise` sums over 4+ variables. No solver option found fixes it.

## Key finding: the MSiMBA normalizer is (almost) canonical

`MultibitSimplifier::simplify` reduces both the 14 KB original and the
candidate result to the same normal form in **milliseconds** (e.g.
`v0+4078378071895433826`). Sampled 12 expressions: `simplify(orig)` and
`simplify(ground_truth)` are identical in all 12 cases **up to top-level term
ordering** (`x+C` vs `C+x`). So:

> **Canonical-form comparison is a practical prover for the semi-linear
> class:** simplify both sides, sort the top-level sum terms, compare
> strings. Milliseconds instead of 30 s timeouts.

Soundness caveat: this trusts the normalizer. Mitigation: fuzz the
normalizer against Z3 **at 8-bit** (where Z3 is fast and complete for these
shapes): random semi-linear expressions → simplify → check
(canonical-equal ⇒ Z3 UNSAT) and (canonical-different ⇒ Z3 SAT or fast-check
mismatch).

## Could E-Graphs be used to do the prove?

**Yes in principle — equality saturation is a sound proof method** (two
expressions in the same e-class after saturation with sound rewrite rules are
provably equivalent; the proof is the rewrite chain). And it fits parts of
this problem well:

- **Bitwise part: excellent fit.** and/or/xor/not with constants → canonical
  DNF via standard boolean rules; AC handling for `&`/`|` comes for free.
- **Arithmetic part: the rule set is the real work.** Sound, terminating
  rules for 64-bit modular arithmetic with `const × bitwise` products
  (coefficient merging `c1*x + c2*x = (c1+c2)*x`, constant folding,
  `c*(m1|m2) = c*m1 + c*m2` for disjoint minterms, …). Writing rules strong
  enough to close `sum c_i*f_i == x+y` essentially means re-implementing the
  MSiMBA algebra inside the e-graph framework.
- **Limitation:** e-graphs only do local rewrites — no case analysis or
  induction. Global properties (the `f_i` jointly forming a partition,
  coefficients cancelling) must be encoded as recognition rules.

**Verdict:** the e-graph *engine* is the easy part (port egg / write one —
weeks); the *rule set* is the hard part and already exists in
`MultibitSimplifier`. E-graphs are the right long-term investment if we want
(a) machine-checkable proofs (rewrite chains), (b) a stronger simplifier, or
(c) coverage beyond the semi-linear class. For the immediate problem,
canonical-form comparison (Phase 2) gets 90% of the benefit for 5% of the
effort.

## Plan

### Phase 1 — Test setup (DONE)

- `tests/run_prove_tests.py`: simba + msimba now run at **32-bit** (16-bit
  fallback if still too slow; note: width does not affect provability, only
  suite speed). Z3 timeout in the suite lowered to 10 s (easy cases prove in
  ms; hard cases are unprovable anyway).
- `--prove` is now wired into the msimba path (`SimplifierRouter.cpp` →
  `proveEquivalent`), so msimba results are Z3-checked like the others.
- `Z3Prover.cpp`: `SIMBA_PRINT_SMT=1` env var dumps the SMT2 for debugging.

### Phase 2 — Canonical-form proving (CORRECTED: the runtime check is vacuous)

**The original Phase 2 idea does not work.** At runtime the candidate is
`res = MultibitSimplifier::simplify(MBA)` (SimplifierRouter.cpp:438), so a
"canonical check" `simplify(MBA) == simplify(res)` is just `res ==
simplify(res)` — idempotency. It passes trivially and proves nothing about
`MBA ≡ res`. There is no fast, sound, non-vacuous runtime proof cheaper than
Z3 (times out) or a SAT solver (Phase 3b).

**What IS non-vacuous and cheap: validate the normalizer against the
dataset's ground truth** (an independent source the normalizer did not
produce). `tests/test_canonical.py` checks
`simplify(expr) == simplify(ground_truth)` (mod top-level term order).

Measured (30/file, 64-bit): **559/559 canonical (100%) wherever the
normalizer succeeds; 0 bad.** The only 11 fails are negative-constant edge
cases (`-3335`, `-6*…`, `^` in the ground truth) the normalizer does not
handle at all — a coverage gap, not a canonicity problem. So:

> Where the MSiMBA normalizer succeeds, its output is canonical and matches
> an independent ground truth. It is trustworthy as the fast proof path for
> the semi-linear class.

**Runtime fast path (already exists, no new code):** the `--accept-unknown`
flag (Z3Prover.cpp) treats a Z3 timeout (UNKNOWN) as proved. Combined with a
short `--timeout`, the msimba path gives:
- easy cases → real Z3 proof (UNSAT) in milliseconds;
- hard cases → accepted after the short timeout (trust-on-timeout,
  justified by the ground-truth validation above).

This is "good enough for now": fast, and defensible because the normalizer is
validated. It is NOT an independent proof of the hard cases — that is
Phase 3b (Kissat) or 3c (proof-by-derivation).

**Decision (made):** `tests/run_prove_tests.py` reports the **honest Z3
metric** — no `--accept-unknown`, hard multi-var cases count as unproved. The
`--accept-unknown` fast path remains available for ad-hoc "good enough"
proving but is not part of the reported metric.

### Phase 3 — Optional: real independent proofs (pick one, later)

- **3a. E-graph prover** (the long-term answer to the e-graph question):
  port egg (or write a minimal e-graph), rule set = boolean canonicalization
  + modular-arithmetic rules extracted from `MultibitSimplifier`. Output:
  rewrite-chain proof. Effort: weeks.
- **3b. SAT proving with a dedicated SAT solver (Kissat):** bit-blast the
  conjecture (Z3 as encoder only), Tseitin to CNF, solve with Kissat.
  Complete and sound, no new algebra. **Detailed plan: `KISSAT_PROVE_PLAN.md`**
  (de-risked: Z3+Bitwuzla QF_BV both time out; Z3 `dimacs()` doesn't work on
  the bit-blasted goal, so Tseitin is done in-house; Kissat is fetchable,
  CaDiCaL already built as fallback). Effort: ~3-4 days.
- **3c. Proof by derivation:** make `MultibitSimplifier` log each rewrite
  step and verify each step `Ei ≡ Ei+1` with Z3 (each step is a small local
  rewrite, which Z3 *can* handle). Effort: days-weeks (requires
  restructuring the simplifier to emit fine-grained steps).

## Files

- `tests/run_prove_tests.py` — prove test suite (32-bit simba/msimba, 8-bit
  gamba, 10 s Z3 timeout; honest Z3 metric).
- `tests/test_canonical.py` — ground-truth canonical validation of the
  MSiMBA normalizer (the non-vacuous Phase 2 artifact).
- `SimplifierRouter.cpp` — msimba branch now Z3-proves under `--prove`.
- `Z3Prover.cpp` — `SIMBA_PRINT_SMT=1` debug dump.
- Benchmarks (scratch, `/tmp/`): `z3_tactic_bench`, `z3_presimp_bench`,
  `z3_bitblast_bench`, `z3_decomp_bench` + `conj.smt2` (64-bit),
  `conj32.smt2`, `conj16.smt2`.
