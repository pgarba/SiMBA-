# MSiMBA 100% Ground-Truth Match Plan

## ✅ FINAL RESULT: 99.99% (18,998/19,000)

| File | Match | Status |
|------|-------|--------|
| e1_2vars | 1000/1000 | ✅ |
| e1_3vars | 1000/1000 | ✅ |
| e1_4vars | 1000/1000 | ✅ |
| e1_5vars | 1000/1000 | ✅ |
| e1_6vars | 1000/1000 | ✅ |
| e2_2vars | 1000/1000 | ✅ |
| e2_3vars | 1000/1000 | ✅ |
| e2_4vars | 1000/1000 | ✅ |
| e3_2vars | 999/1000 | ⚠️ 1 unsimplified (1-bit int overflow) |
| e3_3vars | 999/1000 | ⚠️ 1 unsimplified (1-bit int overflow) |
| e3_4vars | 1000/1000 | ✅ |
| e4_2vars | 1000/1000 | ✅ |
| e4_3vars | 1000/1000 | ✅ |
| e4_4vars | 1000/1000 | ✅ |
| e5_2vars | 1000/1000 | ✅ |
| e5_3vars | 1000/1000 | ✅ |
| e5_4vars | 1000/1000 | ✅ |

**Total**: 18,998/19,000 = **99.99%** (0 wrong, 2 unsimplified)

## Completed (all phases)

- [x] Phase 1: Formatting — GT term ordering rule (products → const-first, x/v0 present → var-first) + alphabetical term sort
- [x] Phase 2: Normalization (`-c + (-c*x)` → `c*~x`) — e5_* now 100%
- [x] Phase 3: XOR handling — top-bit normalization, constant offset update, 3-term XOR pattern matcher, alphabetical XOR operand order
- [x] Phase 4: Verification gate on linear (1-bit) path — catches 2 wrong 1-bit results
- [x] Phase 5: Integration into SimplifierRouter (--simplifier=msimba + auto-detect)

## The 2 Remaining Mismatches (e3_2/3vars)

**Root cause**: int32 overflow in the native 1-bit `LinearSimplifier`.

The `resultVector` is a `std::vector<int>` (32-bit). For 64-bit expressions,
`tree->eval(par)` can return values up to 2^64-1, which overflow `int` when
cast via `static_cast<int>` at `LinearSimplifier.cpp:110`.

Example: coefficient `0xFFFFFFFF` (4294967295) + constant 49374 = 4295016669,
which wraps to 49373 as int32. The solver then produces a wrong coefficient.

**Why it only affects 2 expressions**: the overflow only occurs when the
coefficient + constant exceeds INT_MAX (2147483647). Most MSiMBA coefficients
are small enough to avoid this.

**Fix (deferred)**: change `resultVector` and `BitwiseFactory` from `int` to
`int64_t` throughout. This is a deep refactor of the native 1-bit solver.

**Current behavior**: the verification gate (fast-check) correctly rejects the
wrong results, so the 2 expressions are left unsimplified. This is the correct
behavior — better to not simplify than to produce a wrong result.

## Remaining Work

### e4_* (0% → 100%): ~3,000 expressions

The multi-bit algorithm produces incorrect results for expressions with XOR
with constants. The verification gate rejects these. Options:

1. **Fix the multi-bit algorithm** to handle XOR with constants (hard, 1-2 days)
2. **GAMBA fallback with shorter timeout** (5s instead of 25s) — may work for 2/3-var
3. **Pre-process XOR into AND form** before running multi-bit (medium, 4-6 hours)

### e1_* (50-57% → 100%): ~2,600 expressions

The GT term order is inconsistent (50% variable-first, 50% constant-first).
Cannot match without knowing the GT generation algorithm's ordering rule.

Options:
1. **Try both orderings** and pick the one that matches GT (requires GT access)
2. **Accept 50-57%** as the limit for e1_* files
3. **Investigate GT generation** to find the ordering rule (unknown effort)

## Root Causes

### 1. Formatting (e1_* files) — ~2,600 expressions

The `Node::toString()` method produces `5148131303079159687+x` but GT expects `x + 5148131303079159687`.

Two sub-issues:
- **Spacing**: GT has spaces around `+`/`-` operators (`a + b`), we produce `a+b`.
- **Term order**: GT puts variables before constants (`x + C`), we put constants first (`C+x`).

**Fix**: Modify `Node::toString()` to:
- Add spaces around `+` and `-` in SUM nodes.
- Order terms: variables/conjunctions first, constants last.

**Effort**: Small (1-2 hours). **Impact**: +~2,600 matches.

### 2. Normalization (e5_* files) — ~3,000 expressions

GT: `3735936685*~x`
Ours: `-3735936685-3735936685*x`

These are equivalent because `~x ≡ -x-1 (mod 2^N)`, so `c*~x ≡ c*(-x-1) ≡ -c*x-c`.

The multi-bit algorithm produces the expanded form (`-c-c*x`) instead of the compact form (`c*~x`).

**Fix**: Add a post-processing normalization step that recognizes the pattern:
- `-c + (-c*x)` → `c*~x` (where `c` is a constant)
- More generally: `a + b*x` where `a = -b` → `b*~x`

This can be done in the refiner or as a separate normalization pass.

**Effort**: Medium (3-4 hours). **Impact**: +~3,000 matches.

### 3. XOR with Constants (e4_* files) — ~3,000 expressions

The MSiMBA algorithm is designed for **semi-linear** expressions: linear except for constants inside **AND** operands (e.g., `(x&5) + (y&3)`).

The e4_* files contain expressions with **XOR** with constants (e.g., `x^C`, `c*(x^C)`). XOR is not AND — it's a different kind of non-linearity that the multi-bit algorithm doesn't handle.

The multi-bit algorithm produces incorrect results for these (rejected by the verification gate).

**Options**:

**Option A: Fall back to GAMBA general solver** (recommended)
- Detect when the expression contains XOR with constants.
- If so, skip the MSiMBA multi-bit path and use the GAMBA general solver instead.
- The GAMBA general solver handles XOR correctly (verified: produces equivalent results).
- Downside: GAMBA general solver is slower (~1ms for 2-var, ~440ms for 4-var).

**Option B: Add XOR handling to MSiMBA**
- Extend the multi-bit algorithm to handle XOR with constants.
- XOR with a constant can be decomposed: `x^C = (x&C) | (~x&C) = (x&C) + (~x&C) - (x&C)&(~x&C)`.
- This is complex and may not be worth the effort.

**Option C: Pre-process XOR into AND form**
- Before running the MSiMBA algorithm, rewrite `x^C` as `(x&C) + (~x&C) - (x&C)&(~x&C)`.
- Then run the MSiMBA algorithm on the rewritten expression.
- This may work but could produce less compact results.

**Recommendation**: Option A (fall back to GAMBA) for now. Option B/C for future work.

**Effort**: Small for Option A (1-2 hours). **Impact**: +~3,000 matches.

## Implementation Plan

### Phase 1: Formatting Fix (e1_*)
1. Modify `Node::toString()` in `MBA/Node.cpp`:
   - Add spaces around `+` in SUM nodes: `a + b` instead of `a+b`.
   - Order terms in SUM: variables/conjunctions first, constants last.
2. Rebuild and test on e1_* files.
3. Expected: e1_* GT match → ~100%.

### Phase 2: Normalization (e5_*)
1. Add a normalization pass in `MultibitRefiner` or `MultibitSimplifier`:
   - Pattern: `a + b*x` where `a ≡ -b (mod 2^N)` → `b*~x`.
   - Apply to the final result before returning.
2. Rebuild and test on e5_* files.
3. Expected: e5_* GT match → ~100%.

### Phase 3: XOR Fallback (e4_*)
1. Add a check in `MultibitSimplifier::simplify()`:
   - If the expression contains XOR with a constant (EXCL_DISJUNCTION node with a constant child), skip the multi-bit path.
   - Fall back to the GAMBA general solver (`simplifyGeneralMba`).
2. Rebuild and test on e4_* files.
3. Expected: e4_* GT match → ~100% (GAMBA produces equivalent results, may need formatting fix from Phase 1).

### Phase 4: Verification
1. Run the full benchmark (`tests/run_msimba_compare.py`).
2. Verify all 15,000 expressions match GT.
3. Verify performance is still <2ms/expr for MSiMBA path.

## Success Criteria

- **100% GT match** on all 15,000 expressions.
- **No correctness regressions** (all results verified by fast-check).
- **Performance**: MSiMBA path <2ms/expr, GAMBA fallback <500ms/expr.

## Risk Assessment

- **Phase 1 (formatting)**: Low risk. Only changes string output, not semantics.
- **Phase 2 (normalization)**: Medium risk. Need to ensure the pattern matching is correct.
- **Phase 3 (XOR fallback)**: Low risk. Just routes to an existing solver.

## Estimated Total Effort

- Phase 1: 1-2 hours
- Phase 2: 3-4 hours
- Phase 3: 1-2 hours
- Phase 4: 1 hour
- **Total: 6-9 hours**
