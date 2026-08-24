# Plan: Stronger fast-check to avoid the expensive Z3 prove

## Context

The verification flow (in `Simplifier::simplify` and `LLVMParser::verify`) is:

1. **Fast check** (`probably_equivalent`): random + structured assignments,
   evaluate both sides, reject on any mismatch. Cheap, but *probabilistic*.
2. **Z3 proof** (`verify_mba_unsat` → `proveReplacement`): deterministic,
   bit-blast + SMT, up to the 30 s timeout. This is the expensive path.

The fast check is a *filter* (catch obvious errors early); the Z3 proof is the
*guarantee*. The user's question: can we make the fast check strong enough to
skip Z3?

**Key fact:** a random-sampling fast check can never *replace* Z3 for a
correctness guarantee (it's probabilistic). But we can (a) make it much
stronger so it catches far more real bugs before Z3, and (b) add a *sound*
fast path that lets genuinely-algebraic rewrites skip Z3 safely.

## Measured findings (2026-07-09)

**Z3 is both expensive AND counterproductive on `lifted.ll`:**

| Mode | Wall time | Output size | Notes |
|------|-----------|-------------|-------|
| Default (fast check only) | **7 ms** | **23 instr** | Fast check accepts both rewrites |
| `--prove` (fast check + Z3) | **115 s** | **38 instr** | Z3 times out (30 s budget) on valid rewrites; `AcceptUnknown=false` (default) → rewrites rejected → longer output kept |

Root cause: `proveReplacement()` creates a **new** `z3::context` per call, so
the cached solver is always dropped and recreated (no reuse). The 30 s timeout
is hit on non-trivial bit-blasted expressions, and since `AcceptUnknown=false`
(default), a timeout is treated as "not proven" → the rewrite is rejected.

**Implication:** the default path (fast check only) is already the right
default. The `--prove` path is broken for this workload (slow + worse output).
Phase 1 (stronger fast check) makes the default path safer. Phase 2 (sound fast
path) would let us *document* why the fast check is sufficient for algebraic
rewrites. Phase 3 (Z3 context reuse) would fix the `--prove` path if it's ever
needed.

## What already exists (do not duplicate)

- `MBACache[Hash] = Cand.isValid` (LLVMParser.cpp:2357) — Z3 results are
  already memoized per candidate hash.
- `verify(..., /*DoZ3=*/false)` — the local MBA-pattern path already skips Z3
  for algebraic laws (LLVMParser.cpp:2338-2344).
- `AcceptUnknown` / `timeout` CLI options bound Z3.

## Plan

### Phase 1 — Make the random fast check actually strong (cheap, safe) ✅ DONE
File: `Simplifier.cpp` (`probably_equivalent`, `probably_equivalent_parallel`)
and `CSiMBA.h` (`NUM_TEST_CASES`).

1. **Bump `NUM_TEST_CASES` 16 → 256.** ✅ Done.
2. **Add structured samples** (deterministic, before the random ones): ✅ Done.
   - all-zeros, all-ones,
   - each single variable = 1 with the rest 0 (unit vectors),
   - each variable = modulus-1 (all-ones) with the rest 0.
3. **Keep it deterministic** (fixed splitmix64 seed) so results are
   reproducible. ✅ Done.

**Verification (all passed):**
- `lifted.ll` pipeline output byte-identical (23 instr).
- `opt -passes=verify` → EXIT=0.
- LLVM simplify tests: 6/6 pass.
- Fast-check cost: negligible (ON/OFF difference within process-startup noise).

Risk: none (only *rejects* more; never accepts a wrong result). Cost: a few
extra evals per candidate — negligible vs. Z3.

### Phase 2 — Sound fast path: skip Z3 when the rewrite is a proven algebraic law
File: `LLVMParser.cpp` (`verify`), `Simplifier.cpp`.

The Z3 proof is only needed when the rewrite is *not* a known algebraic law.
Extend the existing `DoZ3=false` path (currently only for `tryMBAPatterns`) so
that any rewrite produced by a *sound* transform (one that is an algebraic
identity by construction) skips Z3. Concretely:
- Tag candidates that come from a sound transform (e.g. the null-byte rewrite,
  the GEP rewrite, the linear-fit rewrite) with a `SoundRewrite` flag.
- In `verify`, if `SoundRewrite` is set, run only the (now-strong) fast check
  and skip Z3.

This is *sound* because the transform is an algebraic identity (always true),
so the fast check is just a sanity net, not the guarantee.

Risk: low — only applies to transforms we can prove are identities. The fast
check still runs as a safety net.

### Phase 3 — Make Z3 itself cheaper (for the cases that still need it)
File: `Z3Prover.cpp`.

1. **Reuse the solver across candidates** (the global `Solver` is already
   cached, but `proveReplacement` builds a *local* `z3::context` each call and
   then `resetZ3Solver()`s — so the cache is defeated). Move to a single
   persistent context (like `Z3CtxGlobal` in LLVMParser) so the solver is
   actually reused.
2. **Shorter timeout for the fast path**: candidates that Z3 can't prove in,
   say, 2 s are unlikely to be worth 30 s; fall back to the (strong) fast
   check + `AcceptUnknown` for those.

Risk: medium — changing the Z3 context lifetime is the kind of thing that has
caused heap corruption before (see the comments in Z3Prover.cpp). Do this
carefully, with the existing `resetZ3Solver`/`abandonZ3Solver` discipline.

## Recommended order (updated with findings)

1. **Phase 1 ✅ DONE** (cheap, safe, immediate win): stronger fast check.
   The default path (fast check only) is now much safer.
2. **Phase 2** (sound, targeted): document + tag algebraic rewrites so the
   fast check is *known* sufficient (not just probabilistically likely).
   Low priority — the default path already skips Z3; this is about
   *documenting* why that's safe.
3. **Phase 3** (risky, needs care): Z3 context reuse. Only worth doing if the
   `--prove` path is ever needed. Currently `--prove` is broken (slow + worse
   output) because of the per-call context creation + 30 s timeout +
   `AcceptUnknown=false`.

## Verification gate (after each phase)

- Rebuild `SiMBA++` (build-linux) and `mba_cli`.
- Re-run the `lifted.ll` pipeline → confirm the output is unchanged (or better)
  and `opt -passes=verify` passes.
- Re-run the GAMBA suite (12 tests) → all pass.
- Re-run the LLVM simplify tests (6/6) → all pass.
- Measure: wall time of the full pipeline before/after (expect a drop from
  skipping Z3 on algebraic rewrites).
