# MSiMBA Integration Plan: Semi-Linear MBA Simplification for SiMBA++

## 1. Background

### 1.1 The Problem

Current linear MBA simplifiers (SiMBA, GAMBA) operate in **1-bit space** and cannot
handle expressions with **nontrivial constants inside bitwise operands**, e.g.:

```
(x & 1111) + (y & 1111)
(x & 5) + (y & 3)
2*(x&5) + 2*(y&3)
```

These are called **semi-linear MBAs** (term from SSLEM). They are "linear" except for
the constants inside the bitwise subexpressions. The N-bit to 1-bit transform that
SiMBA relies on does **not** apply to semi-linear expressions, so the existing
simplifiers either fail or produce suboptimal results.

GAMBA *can* solve semi-linear MBAs by substituting constants with temporary variables
and running the linear solver, but this is **exponential in the number of constants**
and becomes infeasible beyond ~3 variables.

### 1.2 The MSiMBA Solution

The MSiMBA paper (Skees, 2024, arXiv:2406.10016) extends SiMBA with an **N-bit to
N-bit transform**:

1. **Multi-bit signature vector**: Evaluate the expression on `B = {0, 2^i}` for each
   bit `i ∈ {0..N-1}`, producing a vector of size `2^t × N` (where `t` = var count,
   `N` = bit width). This captures the full semantics of the semi-linear expression.

2. **Linearity check**: If the multi-bit vector is uniform across all bit indices
   (after shifting), the expression is actually linear → use the fast 1-bit SiMBA path.

3. **Initial linear combination**: For each bit row, find a linear combination of
   disjoint bitwise conjunctions (basis expressions), each with a coefficient and a
   bit-mask. This is the "naive" solution: `Σ coeff_i * (mask_i & basis_i)`.

4. **Refinement**: A multi-step procedure to merge terms, recover XORs, reduce
   coefficients to -1, and minimize the AST. Key operations:
   - `CanChangeCoefficientTo(old, new, mask)`: Check if `m1*(mask&x)` ≡ `m2*(mask&x)`.
   - `CanChangeMaskTo(coeff, old, new)`: Check if `m*(old&x)` ≡ `m*(new&x)`.
   - Merge disjoint-mask terms with the same coefficient.
   - Recover XORs from inverse-coefficient pairs.
   - Recover ORs from constant pairs.
   - Express 3 terms as 2 (when coefficients sum).

5. **1-bit shortcut**: Substitute bitwise constants with temp variables, run 1-bit
   SiMBA, back-substitute. If the result is simpler, keep it.

### 1.3 Performance (from the paper)

| Tool | 2-var | 3-var | 4-var |
|------|-------|-------|-------|
| ProMBA | 34,722 ms | 33,687 ms | 31,984 ms |
| GAMBA | 14.76 ms | 492 ms | 9,517 ms |
| **MSiMBA** | **0.02 ms** | **0.04 ms** | **0.15 ms** |

MSiMBA is **~1000x faster** than GAMBA on semi-linear MBAs and finds the exact
ground truth in all cases.

### 1.4 Current SiMBA++ Benchmark (MSiMBA dataset)

| Algorithm | Simplified | Failed | Total | Rate |
|-----------|-----------|--------|-------|------|
| GAMBA (general) | 6,127 | 3,123 | 11,250 | 54.5% |
| SiMBA native (1-bit) | 21 | 729 | 750 | 2.8% |

The native 1-bit fitter fails on **97.2%** of semi-linear expressions. GAMBA handles
2-var (100%) and 4-var (100%) but struggles with 3-var (0–38%) and times out on
5/6-var. MSiMBA should handle all of these in <1ms each.

## 2. Current Architecture

```
CSiMBA.cpp (entry point)
  └─ LLVMParser.cpp (LLVM IR → MBA expressions)
       └─ SimplifierRouter.cpp (routing)
            ├─ "native"   → Simplifier.cpp (1-bit SiMBA linear fitter)
            ├─ "general"  → MBA/GeneralSimplifier.cpp (GAMBA general)
            ├─ "external" → Python GAMBA subprocess
            └─ "auto"     → try native, fall back to general
```

Key existing infrastructure:
- **`MBA/Node.h`**: AST node types (Var, Const, Add, Mul, And, Or, Xor, Neg, Not).
- **`MBA/Parser.cpp`**: Parses MBA expression strings into AST.
- **`MBA/Verify.cpp`**: Fast-check verification (evaluate both expressions on random inputs).
- **`Simplifier.cpp`**: 1-bit SiMBA linear fitter (signature vector + linear combination).
- **`MBA/GeneralSimplifier.cpp`**: GAMBA general simplifier (exponential in var count).
- **`MBA/LinearSimplifier.h`**: GAMBA linear simplifier (used by GeneralSimplifier).

## 3. Integration Design

### 3.1 New Files

```
MBA/MultibitSimplifier.h      — Public interface for the MSiMBA solver
MBA/MultibitSimplifier.cpp    — Multi-bit signature vector + initial solution
MBA/MultibitRefiner.h         — Refinement interface
MBA/MultibitRefiner.cpp       — Term merging, XOR/OR recovery, coefficient reduction
MBA/ConstantSubstituter.h     — Constant collection + substitution
MBA/ConstantSubstituter.cpp   — Substitute bitwise constants with temp vars
```

### 3.2 Modified Files

```
SimplifierRouter.cpp          — Add "msimba" routing option + auto-detection
SimplifierRouter.h            — New routing option
CMakeLists.txt                — Add new source files to the build
MBA/mba_cli.cpp               — Add "msimba" command for standalone testing
tests/run_msimba_compare.py   — Add MSiMBA column to the comparison
```

### 3.3 Routing Logic

```
auto-detect:
  1. Parse the expression.
  2. Check if it's semi-linear (has constants inside bitwise operands).
     - If NO → use native 1-bit SiMBA (existing path).
     - If YES → use MSiMBA multi-bit path.
  3. MSiMBA internally checks if the expression is actually linear
     (uniform multi-bit vector) → falls back to 1-bit SiMBA if so.

explicit:
  --simplifier=msimba   → always use MSiMBA
  --simplifier=native   → always use 1-bit SiMBA
  --simplifier=general  → always use GAMBA general
  --simplifier=auto     → auto-detect (default)
```

### 3.4 Algorithm Pipeline (MSiMBA path)

```
Input: MBA expression string, bit width N
  │
  ├─ 1. Parse to AST (MBA/Parser.cpp)
  │
  ├─ 2. Collect variables (t vars)
  │
  ├─ 3. Build multi-bit signature vector
  │     For each bit i ∈ {0..N-1}:
  │       For each var combination c ∈ {0..2^t-1}:
  │         Set each var to (c's bit) << i
  │         Evaluate AST → result
  │         Shift result right by i
  │         Store in vector[bit_i][c]
  │     Vector size: 2^t × N
  │
  ├─ 4. Linearity check
  │     Build a 1-bit linear expression from vector row 0.
  │     Build its multi-bit vector.
  │     Compare (after shifting constant offset per row).
  │     If equal → expression is linear → use 1-bit SiMBA (fast path).
  │
  ├─ 5. Initial linear combination (if not linear)
  │     Subtract constant offset from each row (shifted by bit index).
  │     For each bit row:
  │       For each basis expression (conjunction of vars):
  │         Extract coefficient from vector.
  │         Subtract from remaining vector entries.
  │         Record (coeff, bit_mask) for this basis.
  │     Result: Σ coeff_i * (mask_i & basis_i)
  │
  ├─ 6. Refinement (MultibitRefiner)
  │     a. Merge same-coeff disjoint-mask terms.
  │     b. Try to change coefficients to enable merging.
  │     c. Discard terms with coefficient → 0.
  │     d. Reduce coefficients to -1 where possible.
  │     e. Recover XORs from inverse-coefficient pairs.
  │     f. Recover ORs from constant pairs.
  │     g. Express 3 terms as 2 (coefficient sum).
  │     h. Group same-basis terms → run 1-bit SiMBA on the group.
  │
  ├─ 7. 1-bit shortcut (ConstantSubstituter)
  │     a. Collect bitwise constants (limit: ≤5 unique constants).
  │     b. Unmerge constants (FastConstantUnmerger).
  │     c. Substitute constants with temp vars.
  │     d. Run 1-bit SiMBA on the substituted expression.
  │     e. Back-substitute constants.
  │     f. Constant-fold.
  │     g. If result is simpler (by cost metric), keep it.
  │
  └─ 8. Verify (MBA/Verify.cpp fast-check)
        Evaluate original and simplified on 256 random + corner inputs.
        If mismatch → reject (return original).
```

## 4. Implementation Phases

### Phase 1: Multi-bit Signature Vector + Linearity Check (Week 1)

**Goal**: Build the multi-bit signature vector and detect linear expressions.

**Files**:
- `MBA/MultibitSimplifier.h` — Interface: `simplifyMultibit(expr, bitWidth) → string`
- `MBA/MultibitSimplifier.cpp` — `buildResultVector()`, `isLinearResultVector()`

**Details**:
- Port `InterpretResultVector` from `MultibitSiMBA.cs` (the JIT version is
  Windows-only; the interpreter works everywhere).
- The vector is `uint64_t[2^t * N]` where `t` = var count, `N` = bit width.
- For `t > 20`, the vector is too large → bail out (return original).
- Linearity check: build a 1-bit linear expression from row 0, build its
  multi-bit vector, compare. If equal → delegate to existing 1-bit SiMBA.

**Verification**:
- Unit test: `e(x,y) = x + y` → linear (uniform vector) → 1-bit path.
- Unit test: `e(x,y) = (x&5) + (y&3)` → semi-linear (non-uniform) → multi-bit path.
- Run on `data/MSiMBA/e1_2vars.txt` → should detect all as semi-linear.

### Phase 2: Initial Linear Combination (Week 1–2)

**Goal**: Find the naive linear combination of conjunctions.

**Files**:
- `MBA/MultibitSimplifier.cpp` — `simplifyGeneric()`, `simplifyMultibitGeneric()`

**Details**:
- Subtract constant offset from each row (shifted by bit index).
- For each bit row, iterate over basis expressions (conjunctions of vars).
- Extract coefficient, subtract from vector, record `(coeff, bit_mask)`.
- Build the expression: `Σ coeff_i * (mask_i & basis_i)`.
- This is the "naive" solution — correct but not minimal.

**Verification**:
- `e(x,y) = 2*(x&5) + 2*(y&3)` (mod 2^3) → should produce a valid (if verbose) solution.
- Fast-check verify against the original.

### Phase 3: Refinement (Week 2–3)

**Goal**: Port the `MultibitRefiner` to merge terms and recover structure.

**Files**:
- `MBA/MultibitRefiner.h` — Interface: `simplifyEntry()`, `canChangeCoefficientTo()`, `canChangeMaskTo()`
- `MBA/MultibitRefiner.cpp` — All refinement steps

**Details**:
- Port `SimplifyMultibitEntry`: merge same-coeff terms, reduce term count.
- Port `CanChangeCoefficientTo`: check `m1*(mask&x) ≡ m2*(mask&x)` for all bits.
- Port `CanChangeMaskTo`: check `m*(old&x) ≡ m*(new&x)` for all bits.
- Port `TrySimplifyXor`: recover XORs from inverse-coefficient pairs.
- Port `TrySimplifyOr`: recover ORs from constant pairs.
- Port `TryEliminateUniqueMultibitValues`: express 3 terms as 2.
- Port `FindMinimalMask`: minimize the number of set bits in a mask.

**Verification**:
- `e(x) = 64*(130&x) + 64*(192&x)` → should simplify to `64*(194&x)`.
- `e(x,y) = 980 + (-10*(98&x)) + (10*(-99&x))` → should recover `10*(98^x)`.
- Run on `data/MSiMBA/e2_2vars.txt` → should match ground truth for ~90%+.

### Phase 4: Constant Substitution + 1-bit Shortcut (Week 3)

**Goal**: Port the `ConstantSubstituter` for the 1-bit shortcut.

**Files**:
- `MBA/ConstantSubstituter.h` — Interface: `apply()`, `applyBackSubstitution()`
- `MBA/ConstantSubstituter.cpp` — Collect constants, substitute, back-substitute.

**Details**:
- Collect bitwise constants (constants inside And/Or/Xor/Neg operands).
- Limit: ≤5 unique constants (bail out if more).
- Substitute each constant with a temp var (`um<value>`).
- Run 1-bit SiMBA on the substituted expression.
- Back-substitute constants.
- Constant-fold.
- If the result is simpler (by cost metric), keep it.

**Verification**:
- `e(x) = (x&1111) + (x&-1112)` → should simplify to `x` (linear shortcut).
- Run on `data/MSiMBA/e3_2vars.txt` → should improve ground-truth match rate.

### Phase 5: Integration + Routing (Week 3–4)

**Goal**: Wire MSiMBA into the router and CLI.

**Files**:
- `SimplifierRouter.cpp` — Add `"msimba"` option + auto-detection.
- `SimplifierRouter.h` — New option.
- `CMakeLists.txt` — Add new source files.
- `MBA/mba_cli.cpp` — Add `"msimba"` command.

**Details**:
- Auto-detection: check if the expression has constants inside bitwise operands.
  - If yes → MSiMBA path.
  - If no → existing native 1-bit path.
- Add `--simplifier=msimba` for explicit selection.
- Add `mba_cli msimba <bitCount> <expr>` for standalone testing.
- Update `tests/run_msimba_compare.py` to include the MSiMBA column.

**Verification**:
- Run `tests/run_msimba_compare.py` → MSiMBA should simplify 100% of 2/3/4-var.
- Run `tests/run_tests_llvm_simplify.py` → all 6/6 should still pass.
- Run `tests/run_tests.py` → all existing tests should still pass.

### Phase 6: Performance Optimization (Week 4+, optional)

**Goal**: Optimize the hot paths.

**Ideas**:
- **JIT evaluation**: The C# implementation uses a JIT-compiled evaluator for the
  signature vector (10x faster than interpretation). Port this using `mmap` +
  `mprotect` (Linux) or `VirtualAlloc` (Windows).
- **Early termination**: If the vector is uniform after the first few bits, skip
  the remaining bits (likely linear).
- **Memoization**: Cache signature vectors for subexpressions.
- **Parallelism**: Evaluate different bit rows in parallel (independent).

## 5. Key Design Decisions

### 5.1 Where to put the code

**Option A**: New files in `MBA/` (alongside the GAMBA port).
- Pro: Keeps the GAMBA port self-contained.
- Con: MSiMBA is not GAMBA — it's an extension of SiMBA.

**Option B**: New files in the root directory (alongside `Simplifier.cpp`).
- Pro: MSiMBA is an extension of the native SiMBA path.
- Con: Mixes native and GAMBA code.

**Decision**: **Option A** (`MBA/` directory). The MSiMBA code is a new solver
that operates on the same AST as the GAMBA port. It's cleaner to keep it with the
other solver code. The router will call it just like it calls the GAMBA general
simplifier.

### 5.2 AST representation

The MSiMBA algorithm needs to:
- Evaluate the AST on specific inputs (for the signature vector).
- Build new ASTs (for the solution).
- Manipulate ASTs (for the refinement).

The existing `MBA/Node.h` AST is sufficient. The evaluator can be a simple
recursive function (no JIT needed for the initial implementation).

### 5.3 Bit width

The MSiMBA algorithm works for any bit width. The signature vector size is
`2^t × N` where `t` = var count, `N` = bit width. For `t=4, N=64`, the vector is
`256 × 64 = 16,384` entries (128 KB) — very manageable.

For `t=6, N=64`, the vector is `64 × 64 = 4,096` entries — also fine.

For `t=10, N=64`, the vector is `1024 × 64 = 65,536` entries (512 KB) — still OK.

For `t=20, N=64`, the vector is `1,048,576 × 64 = 67M` entries (512 MB) — too large.
**Limit**: `t ≤ 15` (32,768 × 64 = 2M entries, 16 MB).

### 5.4 Verification

The MSiMBA algorithm is **sound by construction** (the signature vector captures
the full semantics). However, we still apply the fast-check verification as a
safety net (in case of implementation bugs).

### 5.5 Cost metric

The refinement uses a cost metric to compare solutions. The C# implementation
uses `Metric::String` (string length) by default. We'll use the same:
- **Cost** = number of characters in the expression string.
- **Simpler** = shorter string.

## 6. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Signature vector too large for high var count | Medium | High | Limit to t ≤ 15; bail out gracefully |
| Refinement produces incorrect result | Low | High | Fast-check verification as safety net |
| Performance regression on linear MBAs | Low | Medium | Linearity check → fast 1-bit path |
| C# → C++ porting bugs | Medium | Medium | Unit tests on known examples from the paper |
| Constant substitution name conflicts | Low | Low | Use `um<value>` prefix; check against existing vars |

## 7. Success Criteria

1. **Correctness**: MSiMBA simplifies 100% of 2/3/4-var expressions in
   `data/MSiMBA/` to a valid equivalent (fast-check verified).
2. **Quality**: MSiMBA matches the ground truth for ≥90% of 2-var expressions
   (GAMBA achieves 90.7% on e2_2vars).
3. **Performance**: MSiMBA simplifies each expression in <1ms (vs GAMBA's
   14.76ms for 2-var, 9517ms for 4-var).
4. **No regression**: All existing tests (`run_tests.py`, `run_tests_llvm_simplify.py`)
   still pass.
5. **Integration**: `--simplifier=auto` correctly routes semi-linear expressions
   to MSiMBA and linear expressions to the native 1-bit path.

## 8. Reference Implementation

The C# reference implementation is at `external/MSiMBA/`:
- `Mba.Common/MSiMBA/MultibitSiMBA.cs` (2336 lines) — Main algorithm
- `Mba.Common/MSiMBA/MultibitRefiner.cs` (658 lines) — Refinement
- `Mba.Common/MSiMBA/ConstantSubstituter.cs` (140 lines) — Constant substitution
- `Mba.Common/MSiMBA/FastConstantUnmerger.cs` (473 lines) — Constant unmerging
- `Mba.Common/MSiMBA/AstJit.cs` (307 lines) — JIT evaluator (Windows-only)

The paper is at `plans/MSIMBA_INTEGRATION_PLAN.md` (this file) and the PDF is at
`.pi-web/attachments/attachment-20260824-111015-799-1-2406.10016v2.pdf`.
