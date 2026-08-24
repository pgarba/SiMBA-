# GAMBA Missing-Operator Support Plan (`>>`, `/`, `%`)

**Goal.** Add support for the operators `>>` (shift right), `/` (integer divide) and `%`
(integer remainder) to the GAMBA expression language — in the vendored Python oracle
(`external/GAMBA`) **and** the native C++ port (`MBA/`) — so that MBAs containing these
operators (already emitted by `LLVMParser::getASTAsString`) are routed to the native
`general` simplifier instead of silently falling back to the native linear path.

### Implementation status

- **Tier 1 (Phase A) — DONE & verified.** Both the oracle and the native port desugar
  `var >> const`, `var / 2^k`, `var % 2^k` to bit sums (constant-coefficient, linear).
  Differential suite green (104/104), div/rem semantics 8746/0 MISMATCH, full suite 11/11.
- **Tier 2 (Phase B) — DONE, NATIVE-ONLY.** Per the maintainer's clarification, Tier 2 was
  implemented **only in the native C++ port** (`MBA/`); the vendored Python oracle was **not**
  modified for Tier 2 (it still rejects the non-desugarable cases). First-class `RSHIFT`/`UDIV`/`UREM`
  node types (9/10/11) with exact unsigned mod-2^B eval, NONLINEAR state (opaque leaves for the
  general simplifier), and a minimal sound rewrite set. Verified natively via `mba_cli`
  `parse`/`general`/`eval` spot-checks and `MBA/test_tier2_semantics.py` (4000 checks @ 8-bit,
  1600 @ 16-bit, 0 MISMATCH). The differential suite (`MBA/diff_parse.py`) was **not** extended
  with Tier 2 cases (the oracle won't implement them); instead its existing "rejected" cases that
  the native port now parses are listed in `TIER2_MORE_PERMISSIVE` (cpp-parses/py-rejects is the
  expected outcome). Full suite 11/11; `SiMBA++` rebuilds clean (NMAKE_EXIT=0).

---

## 1. Current state (verified against the code)

| Layer | `<<` | `>>` | `/` | `%` |
|---|---|---|---|---|
| GAMBA parser — Python oracle (`external/GAMBA/src/utils/parse.py`) and native (`MBA/Parser.cpp`) | ✅ desugared to `a * 2**b` | ❌ rejected | ❌ rejected | ❌ rejected |
| Z3 backend (`ShuttingYard.cpp::getZ3ExprFromString`) | ✅ `z3::shl` | ⚠️ `z3::lshr` exists, but tokenization is single-char so `>>` becomes two `>` tokens (broken) | ✅ `z3::udiv` | ❌ not tokenized ("Unknown Token" fatal) |
| `LLVMParser::getASTAsString` (`LLVMParser.cpp:1728-1772`) | emits `<<` | already emits `>>` | already emits `/` | already emits `%` |

Key facts:

- The parser grammar (both oracle and native) is `| → ^ → & → << → +/- → * → unary ~/- → ** →
  terminal`. `<<` desugars to `PRODUCT(base, POWER(2, rhs))` (`MBA/Parser.cpp:332-343`).
- The parser **already parses `[digits]` bit-suffixes** as variable names: `a[3]` becomes a
  `VARIABLE` with `vname = "a[3]"` (`MBA/Parser.cpp:164-171`). Bit decomposition therefore
  needs **no new node types**.
- `getASTAsString` emits `>>` for **both** `LShr` and `AShr` (`LLVMParser.cpp:1753-1759`, the
  AShr arm carries a `// Should work in python...` comment), `/` for both `UDiv` and `SDiv`,
  and `%` for both `URem` and `SRem`. Constants are emitted in decimal.
- Rejected strings make `TrySelectedSimplifier` return `false` → the caller keeps the original
  native linear path. That is the silent fallback this plan removes.
- Verification: `fastCheckEquivalent` (`MBA/Verify.cpp:84-122`) assigns independent random
  values per variable name; `proveEquivalent` (`MBA/Verify.cpp:124-148`) declares each
  variable as a full-width `bv_const` (`ShuttingYard.cpp:909`).

### Algebraic status of the operators

- `<<` is a ring operation: `a << k ≡ a * 2**k`.
- `>>` / `/` / `%` **by a constant power of two** are expressible via bit decomposition
  (variables `a[i]` are the bits of `a`, `B` = bit count):
  - `a >> k  ≡ Σ_{i=k}^{B-1} a[i] · 2^(i-k)`   (`k = 0` → `a`; `k ≥ B` → `0`)
  - `a % 2^k ≡ Σ_{i=0}^{k-1} a[i] · 2^i`        (`k = 0` → `0`; `k ≥ B` → `a`)
  - `a / 2^k ≡ a >> k`  (unsigned semantics; values are reduced mod `2^B`)
- **General** division/remainder (non-power-of-two divisor, or non-constant divisor) is **not**
  polynomial and cannot be desugared into the ring. It requires first-class operator nodes
  (Tier 2).

### Performance note (measured — important)

The bit-desugaring produces a sum of `B - k` bit terms (one per bit-sliced variable). The GAMBA
simplifier — **both** the Python oracle and the native C++ port — is **exponential in the number
of bit-sliced variables**, so wide values are impractically slow:

| Expression | Terms | Native `simplify` | Python oracle `simplify` |
|---|---|---|---|
| `a >> 3` @ 24-bit | 21 | ~2s | ~63s |
| `a >> 3` @ 26-bit | 23 | ~8s | ~295s |
| `a >> 3` @ 30-bit | 27 | ~160s | — |

Two findings:

1. **Linearity is required.** The desugared term `a[i] * 2**k` must be built as a **single
   constant coefficient** (`a[i] * C`, `C = 2**k`), *not* as a `2**k` POWER node. A POWER
   coefficient makes the whole expression **nonlinear**, pushing the general simplifier into its
   exhaustive `2**vnumber` enumeration path (hang/OOM for 32-bit+). Folding `2**k` into a
   constant keeps the expression **linear** (the fast path). Implemented in `MBA/Parser.cpp`
   `bitTerm` (via `MBAOps::pow2`) and the oracle `parse.py` `__bit_term` (kept byte-identical
   for the differential suite).
2. **The linear path is still exponential** in the number of terms (~4x per +2 terms, i.e.
   `2^(n/2)`). This is a **fundamental** characteristic of the GAMBA simplifier (confirmed in
   both the Python oracle and the native port), not a port bug. The native port is ~30x faster
   than the Python oracle. Consequence: **8-16-bit values simplify quickly; 32-bit and 64-bit
   desugared values are impractically slow** (minutes to hours). Tier 2 (first-class operator
   nodes, Phase B) is the path to wide-value support — it avoids the bit-explosion entirely.

---

## 2. Tier 1 — constant power-of-two `>>` / `/` / `%` via bit desugaring (small, do first)

**Scope decision D1.** Only desugar when the LHS is a **plain variable** (a `vname` with no
`[...]` bit-suffix — a full modulus-width value whose bits can be addressed as `name[i]`) and
the RHS is a **non-negative constant**: any constant for `>>` (the shift amount itself), and a
**power of two** for `/` and `%`. Everything else (bit-sliced/compound LHS, non-power-of-two
or non-constant RHS, division by zero) is a **clear parse error** — the caller then falls back
to the native path exactly as today. Rationale: bit decomposition of a compound LHS is not
possible; failing loudly is preferable to a wrong desugaring.

> **Refinement vs. the original plan text (implemented):**
> 1. `>>` accepts **any non-negative constant** as the shift amount (not only powers of two) —
>    the RHS of `a >> k` is the shift amount `k` itself, so `a >> 3` (a common `lshr x, 3`)
>    must work. Only `/` and `%` require a power-of-two RHS.
> 2. `/` and `%` sit at the **multiplicative (product) level** of the grammar — the same level
>    as `*`, i.e. *above* `+`/`-` — matching C precedence (`a / 4 + b` = `(a/4) + b`). `>>`
>    stays at the **shift level** (below `+`/`-`), also matching C (`a >> 1 + b` = `a >> (1+b)`).

Constant/constant operands (`C op D`) are folded to the integer result (floor semantics for
`/` and `%` on non-negative values).

### Steps

1. **Oracle first** — `external/GAMBA/src/utils/parse.py` (source of truth for the
   differential suite; the vendored GAMBA is retained and not deleted):
   - Add `>>`, `/`, `%` to the grammar at the same precedence level as `<<` (between
     CONJUNCTION and SUM). Tokenize `>>` before a bare `>`.
   - Parse-time desugaring per D1:
     - `VAR >> C` with `C = 2^k`, `0 ≤ k < B` → sum of bit terms `a[k] + a[k+1]*2 + ...`
     - `VAR / C` with `C = 2^k` → same as `VAR >> k`
     - `VAR % C` with `C = 2^k` → low-bit sum `a[0] + a[1]*2 + ... + a[k-1]*2^(k-1)`
     - Edge cases: `k = 0` (identity / zero), `k ≥ B` (zero / identity), `C = 0` → error
       "division by zero", non-power-of-two → error "division/remainder by non-power-of-two
       not supported", non-variable LHS → error "only `var >> const_pow2` is supported".
   - Round-trip: `to_string` of the desugared tree must match the native port byte-for-byte
     (existing D4 requirement).
2. **Native port** — `MBA/Parser.cpp` mirrors the oracle exactly (same grammar slot, same
   desugar, same error messages). 64-bit safety via `MBAOps::reduce` / `widthMask` (the D2
   convention; no `1ULL << bitCount`).
3. **Z3 tokenizer fix** — `ShuttingYard.cpp`:
   - `exprToTokens` (lines 123-256) is single-char (`std::string(1, c)` at line 252): make it
     scan multi-char operators — `>>` → one `lshr` token, keep `<<` → one `shl` token — and
     add a `%` case.
   - `getZ3ExprFromString` (lines 886-1047) already implements `>` → `z3::lshr`,
     `<` → `z3::shl`, `/` → `z3::udiv`; add `%` → `z3::urem`.
   - This is needed for `--prove` on *original* strings containing these operators
     (`proveEquivalent` feeds the raw original string to the native tokenizer; the desugared
     GAMBA side never contains them).
4. **Differential tests** — extend the `parse` suite in `MBA/run_all_tests.py` with a
   `>>`/`/`/`%` corpus: constant power-of-two at 8- and 64-bit, `k = 0`, `k ≥ B`,
   div-by-zero, non-power-of-two, non-variable LHS (all error cases must error **identically**
   in oracle and port). All 9 suites must stay green.
5. **End-to-end** (tob LLVM 20.1 + Z3 5.0.0.0 build, per the integration plan item 3):
   - `SiMBA++.exe --mba "a >> 3" --simplifier general --bitcount 8` (and 64-bit) → routed,
     simplified, fast-check passes.
   - `--ir <module with lshr / udiv 2^k / urem 2^k> --simplifier general` → no longer
     silently falls back; `verify()` passes.

---

## 3. Tier 2 — first-class `>>` / `/` / `%` nodes (general semantics, bigger)

Needed for variable divisors and non-power-of-two divisors.

> **Implemented NATIVE-ONLY** (per the maintainer's clarification). The vendored Python oracle
> was **not** modified for Tier 2 — it still rejects the non-desugarable cases. Only the native
> C++ port (`MBA/`) gained the first-class operator nodes.

1. **Native port** — `MBA/NodeType.h`, `MBA/Parser.cpp`, `MBA/Node.cpp`, `MBA/GeneralSimplifier.cpp`:
   - New `NodeType` entries: `RSHIFT = 9`, `UDIV = 10`, `UREM = 11` (one family; the generic
     copy/equals/collect-variables handling already works for them).
   - Grammar slot as in Tier 1; when the Tier 1 desugar does not apply, `desugarDivRem` builds
     the operator node instead of erroring (only a constant divisor of zero still errors).
   - `to_string` renders `>>` / `/` / `%` with a `PrecLevel`/`ChildNeedsParens` precedence
     helper (round-trip stable).
   - `applyBinop`: exact **unsigned** semantics — `RSHIFT` is `x >> y` (0 if `y ≥ B`), `UDIV`
     is `x / y`, `UREM` is `x % y` (both 0 if `y == 0`); the result is reduced mod `2^B`.
   - Node state: marked `NONLINEAR` so `GeneralSimplifier` treats them as opaque leaves.
     `simplifySubexpression` skips `refine`/`markLinear` on them (their children are simplified
     but the operator node itself is never rewritten), and `reorderVariables` skips them (the
     operators are not commutative, so their children must never be reordered).
3. **Rewrite rules — implemented as opaque leaves (simpler, equally sound).**
   The Tier 1 desugar already handles the identities at parse time (`a >> 0 → a`, `a / 1 → a`,
   `a % 1 → 0`), so the Tier 2 operator node is only built when the desugar does not apply.
   The node is marked `NONLINEAR` and the general simplifier treats it as an **opaque leaf**
   (its children are simplified, but the operator node itself is never refined/rewritten/reordered).
   The `eval` is exact (unsigned, mod `2^B`), so no rewrite rules are needed for correctness.
   Constant folding (`C op D`, e.g. `3 / 2 → 1`) is a possible future enhancement (the node
   currently stays as `3/2`, which is still evaluated correctly).
   Soundness is enforced by the existing gates — `fastCheckEquivalent` on every non-native
   result and `RouteResult::INVALID` on counterexample; Z3 prove on demand.
4. **Z3** — already covered by the Tier 1 tokenizer fix (`lshr`/`udiv`/`urem` all exist in
   `getZ3ExprFromString`).
5. **Differential tests** — extend the corpus with variable-divisor and non-power-of-two
   cases at 8/64-bit; oracle remains the source of truth.

---

## 4. LLVM-side corrections (apply with either tier)

1. **`AShr` is emitted as `>>`** (`LLVMParser.cpp:1756-1758`). If `>>` means *logical* shift,
   arithmetic-shift MBAs are semantically wrong for values with the top bit set.
   **Decision: exclude `AShr` from the routed string path** (treat as unsupported → candidate
   falls back to the native path). Simpler and safer than a new `>>s` token; revisit only if
   `AShr` MBAs turn out to be common.
2. **`SDiv`/`SRem` are emitted as `/`/`%`** — unsigned semantics only (signed division rounds
   toward zero; signed remainder carries the dividend's sign). **Decision: document the
   unsigned assumption** (MBA values are interpreted mod `2^B`); the fast-check gate catches
   any mismatch on the routed path.
3. No other `getASTAsString` changes needed — it already emits the operators.

---

## 5. Acceptance criteria

1. **Tier 1:** `--mba` and `--ir` on expressions with `>>`/`/`/`%` by constant power-of-two
   are routed to the `general` path, produce verified results at 8- and 64-bit, and match the
   Python oracle (differential suite green, all 9).
2. **Tier 2:** general-divisor cases parse, evaluate, and verify (fast-check + Z3); no silent
   fallback; solve rate on the existing 7 benchmark datasets unchanged, `err == 0`.
3. `--prove` works on strings containing `>>` (Z3 tokenizer fix).
4. Linear-MBA results unchanged vs. the current baseline (no regression).

## 6. Risks & mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| Oracle `eval` semantics (numpy true division vs. floor) | Wrong oracle → wrong differential gate | Exact integer floor semantics in the oracle eval; differential tests on div/rem corpora. |
| 64-bit overflow in desugared constants / `2^k` | Silent corruption at 64-bit | `MBAOps::reduce`/`widthMask` (D2 convention); 64-bit-specific corpus cases. |
| Compound LHS silently desugared wrongly | Non-equivalent results | Tier 1 rejects non-variable LHS with a parse error (D1); fast-check + Z3 as backstop. |
| Z3 declares `a[i]` as full-width `bv_const` | Counterexamples assign non-1-bit values to `a[i]` | Sound for transformation checking (both sides desugared identically, identity holds over all assignments); cosmetic only in printed counterexamples. |
| `AShr`/`SDiv`/`SRem` semantic conflation | Wrong simplification for signed/top-bit cases | Exclude `AShr` from the routed path; document the unsigned assumption; fast-check gate. |
| Scope creep of Tier 2 rewrite rules | Unsound "simplifications" | Minimal rule set (folding + identities only); `RouteResult::INVALID` never reports an unverified result. |

## 7. Order of work

- **Phase A (Tier 1):** oracle parser → native parser → Z3 tokenizer fix → differential corpus
  → end-to-end (`--mba`, `--ir`, `--prove`).
- **Phase B (Tier 2, NATIVE-ONLY):** native nodes (`RSHIFT`/`UDIV`/`UREM`) → minimal rules →
  native `mba_cli` spot-checks (`parse`/`general`/`eval`) → `test_tier2_semantics.py`. The
  vendored oracle is **not** modified for Tier 2, and the differential suite is **not** extended
  with Tier 2 cases (see the `TIER2_MORE_PERMISSIVE` note in `MBA/diff_parse.py`).
- **Phase C:** LLVM-side `AShr` exclusion + unsigned-semantics note → README update (point at
  this plan) → CI differential job unchanged (runs the same `MBA/run_all_tests.py`).

> Environment notes (from `plans/GAMBA_INTEGRATION_PLAN.md` item 3): configure `build/` with
> the tob LLVM 20.1 + Z3 5.0.0.0 tree (`-DZ3_DIR=…/tob_libraries/z3/lib/cmake/z3`); the
> lightweight `mba_cli` differential tool is built via `MBA/build_general.bat` (no Z3 —
> `proveEquivalent` degrades to a no-op there).
