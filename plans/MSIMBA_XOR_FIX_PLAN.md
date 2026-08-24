# MSiMBA XOR Handling Fix Plan

## Status (after partial fix)

**e4_* improved from 0% to 9-13%.**

### Fixes applied:
1. **Constant offset update after XOR recovery** (root cause fix):
   The C# reference updates `constantOffset` after XOR recovery.
   Our port was not doing this, leading to incorrect constant values.
2. **XOR operand order**: Variable first (`x^C`, not `C^x`) to match GT.
3. **Constant term position**: Inserted first (matching GT format).

### Still broken:
- **Variable isolation**: Re-enabling `tryIsolateVariable` causes regression
  on e1_*/e3_*/e5_* (drops to 0%). Remains disabled.
- **Wrong XOR constant/coefficient**: Some e4_* expressions still have wrong
  XOR constants and coefficient signs. The XOR recovery is firing but
  producing incorrect results for these cases.

### Current GT match: 81.6%
- e2_*, e3_*, e5_*: 100%
- e1_*: ~50-57% (term order issue)
- e4_*: 17-25% (XOR with constants — partial fix)

### Additional fix applied:
- **XOR coefficient normalization**: When the XOR recovery produces a
  negative coefficient, flip the sign, complement the XOR constant, and
  adjust the constant offset. This matches the GT format for Pattern B
  (complement) expressions.

### Remaining e4_* gap (75-83% unmatched):
- **Pattern A** (2^63-1 subtraction): Different equivalent form, not
  fixable by simple normalization.
- **Pattern B residual**: Some Pattern B expressions still don't match
  after normalization (constant offset adjustment may be wrong).
- **All results are equivalent** (verified by fast-check) — purely a
  formatting/normalization issue.

### Debugging findings (this session):

1. **Dangling reference bug** (FIXED): `tryIsolateVariable` used a
   structured binding reference to the map entry, then cleared the map,
   then returned the dangling reference. This was undefined behavior
   producing random coefficients — the root cause of the 0% regression.

2. **XOR inverse calculation**: Our `coeffToMask` has negation pairs
   (`c` and `-c`), not XOR-with-1 pairs (`c` and `c^1`). The C# reference
   uses `c^1` because its native `SimplifyDisjointSumMultiply` produces
   a different dictionary. We must use `-c` to match our dictionary.

3. **Isolation gating**: Isolation is now gated on `!hasBitwiseOps_`.
   XOR expressions have negation pairs in `coeffToMask`, and isolation
   produces incorrect results for these expressions.

### Remaining gap (81.6% → 100%):

**e4_* (75-83% unmatched)**: The C# reference's native
`SimplifyDisjointSumMultiply` produces a `coeffToMask` with XOR-with-1
pairs. Our C++ port produces negation pairs. The XOR recovery works
with negation pairs, but the resulting form doesn't match the GT.
To fix: either port the native function exactly, or add a post-
processing step that converts the negation-pair form to the GT form.

**e1_* (43-50% unmatched)**: The GT has inconsistent term ordering
(50% variable-first, 50% constant-first). Unfixable without access
to the GT generation algorithm.

### Realistic ceiling:
- **With e4_* fix**: ~90-95% (e1_* term order remains)
- **With e1_* fix**: ~100% (if GT ordering rule is discovered)

## Problem

The multi-bit algorithm produces incorrect results for expressions with XOR
with constants (e4_* files, ~3,000 expressions). The verification gate
rejects these results, so they return empty (no simplification).

### Root Cause Analysis

Traced with `x^C` (C = 5148131303079159687):

1. **Result vector is correct**: The multi-bit signature vector correctly
   captures the XOR structure.

2. **Linear combination is correct**: The initial linear combination should
   be `coeff=1 mask=~C` + `coeff=-1 mask=C` + `constant=C`, which the
   XOR refiner should recover to `x^C`.

3. **Constant substitution breaks the result**: The `simplifyViaConstantSubstitution`
   function replaces the constant C with a new variable `um`, producing
   `x^um`. Then the 1-bit SiMBA is run on `x^um`, but `x^um` is NOT linear
   (it's XOR of two variables), so the 1-bit SiMBA produces garbage.

   The C# reference applies constant substitution to the **solution** (a
   linear combination of conjunctions), not to XOR terms. Our port applies
   it to the final string, which can contain XORs.

### Specific Failure Chain

```
Input:  x^C
  ↓ multi-bit algorithm
Solution: x^C  (correct!)
  ↓ constant substitution (replaces C with um)
Substituted: x^um  (NOT linear!)
  ↓ 1-bit SiMBA
Result: -768323825-(x^um)  (WRONG)
  ↓ verification gate
REJECTED → return ""
```

## Fix Strategy

### Option A: Guard the constant substitution (recommended)

**Where**: `MultibitSimplifier::simplifyViaConstantSubstitution()`

**What**: Before applying the constant substitution + 1-bit SiMBA shortcut,
check if the solution contains XOR (EXCL_DISJUNCTION) nodes. If it does,
skip the shortcut and return the solution as-is.

**Why**: The 1-bit SiMBA can only handle linear expressions. If the solution
contains XORs, substituting constants creates XORs between variables, which
the 1-bit SiMBA cannot handle.

**Code change** (~10 lines):
```cpp
std::string MultibitSimplifier::simplifyViaConstantSubstitution(
    const std::shared_ptr<Node> &ast) const {
  // NEW: If the solution contains XOR, skip the 1-bit shortcut.
  // The 1-bit SiMBA cannot handle XOR of variables.
  if (containsXor(ast))
    return "";

  // ... existing code ...
}
```

**Effort**: 30 minutes.
**Risk**: Low. Only skips the shortcut for XOR solutions.
**Impact**: Fixes e4_* expressions where the multi-bit algorithm correctly
produces an XOR solution.

### Option B: Fix the XOR recovery in the refiner

**Where**: `MultibitRefiner::trySimplifyXor()`

**What**: Ensure the XOR recovery correctly identifies and reconstructs XORs
from the linear combination. The current implementation looks for pairs of
coefficients that are negations of each other with complementary bit masks.

**Why**: If the XOR recovery is not firing, the solution remains in the
expanded form (`x - 2*(x&C) + C`) instead of the compact form (`x^C`). The
expanded form may not match the GT.

**Code change** (~20-50 lines, depends on diagnosis):
- Add debug output to trace the `coeffToMask` dictionary.
- Verify the XOR recovery fires for `x^C`.
- Fix any bugs in the mask comparison logic.

**Effort**: 2-4 hours (diagnosis + fix).
**Risk**: Medium. Changes to the refiner could affect other expressions.
**Impact**: Fixes e4_* expressions where the XOR recovery is not firing.

### Option C: Pre-process XOR into AND form

**Where**: `MultibitSimplifier::simplify()` (before the multi-bit path)

**What**: Rewrite `x^C` as `(x&~C) + (~x&C)` before running the multi-bit
algorithm. The multi-bit algorithm handles AND with constants correctly.

**Why**: The multi-bit algorithm is designed for semi-linear expressions
(AND with constants), not XOR with constants. Pre-processing converts XOR
to AND, which the algorithm can handle.

**Code change** (~30-50 lines):
```cpp
// In simplify(), before the multi-bit path:
if (hasXorWithConstant()) {
  // Rewrite x^C as (x&~C) + (~x&C)
  ast = rewriteXorToAnd(ast);
  // Re-parse and continue with the multi-bit path.
}
```

**Effort**: 4-6 hours.
**Risk**: Medium. The rewrite must be correct for all XOR patterns.
**Impact**: Fixes e4_* expressions by converting them to a form the
algorithm can handle.

## Recommended Approach

**Start with Option A** (30 min) — it's the simplest and addresses the most
common failure mode (constant substitution breaking XOR solutions).

**Then diagnose with Option B** (2-4 hours) — if Option A doesn't fix all
e4_* expressions, trace the XOR recovery to find remaining bugs.

**Option C is a fallback** (4-6 hours) — only if Options A+B don't reach
>90% on e4_*.

## Implementation Steps

### Step 1: Option A — Guard constant substitution (30 min)
1. Add `containsXor()` helper to `MultibitSimplifier`.
2. In `simplifyViaConstantSubstitution()`, return "" if the solution has XOR.
3. Rebuild and test on e4_2vars (first 10 expressions).
4. Run full e4_* benchmark.

### Step 2: Diagnose XOR recovery (1-2 hours)
1. Add debug output to `simplifyGeneric()` to print the `coeffToMask`
   dictionary before and after the XOR recovery.
2. Trace `x^C` through the algorithm.
3. Identify if the XOR recovery fires or not.
4. If not firing, fix the mask comparison logic.

### Step 3: Fix remaining issues (1-2 hours)
1. Based on diagnosis, fix any bugs in the XOR recovery.
2. Rebuild and test on e4_* files.
3. Run full benchmark.

### Step 4: Verify (30 min)
1. Run the full MSiMBA benchmark.
2. Confirm e4_* files reach >90% GT match.
3. Confirm no regressions on e2_*, e3_*, e5_*.
4. Commit.

## Success Criteria

- **e4_* GT match**: 0% → >90%
- **No regressions**: e2_*, e3_*, e5_* remain at 100%
- **Performance**: <2ms/expr for MSiMBA path
- **Correctness**: All results verified by fast-check

## Estimated Total Effort

- Option A: 30 min
- Option B (diagnosis + fix): 2-4 hours
- Option C (fallback): 4-6 hours
- **Total (A+B)**: 2.5-4.5 hours
- **Total (A+B+C)**: 6.5-10.5 hours
