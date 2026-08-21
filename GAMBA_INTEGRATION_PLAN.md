# GAMBA → SiMBA++ Integration Plan (Native C++ Port)

**Goal.** Extend the SiMBA++ C/C++/LLVM project so it can simplify **nonlinear** mixed
Boolean–arithmetic expressions (MBAs), by porting the vendored Python **GAMBA** extension
(`external/GAMBA`) to native C++ and wiring it into the existing CLI and LLVM pass.

**Chosen strategy: Track 2 — Native C++ port.** (The lower-risk "external process" track was
considered and deferred; the vendored Python GAMBA is retained as the *reference oracle* for
differential testing throughout.)

---

## 1. Current state (verified against the code)

### SiMBA++ (C++ project, repo root)
- Builds via `CMakeLists.txt` into `SiMBA++` (CLI), `LSiMBA++` (static lib), `SiMBAPass`
  (shared LLVM pass). C++17. Depends on LLVM + Z3.
- `Simplifier.cpp/.h` — native **linear** MBA simplifier (ported from the original Python
  SiMBA). `simplify_linear_mba`, fast-check (`probably_equivalent`), Z3
  (`verify_mba_unsat` → `Z3Prover`), plus an `external_simplifier()` that shells out to a
  Python script (the pre-existing "external GAMBA" hook).
- `LLVMParser.cpp/.h` — parses LLVM IR, detects MBAs, builds an AST, converts it to an
  expression string via `getASTAsString`, then simplifies (native or external) and verifies.
  Already carries an `--external-simplifier` option.
- `MBAChecker`, `Z3Prover`, `ShuttingYard` (shunting-yard), `Modulo`, `BitwiseList`,
  `include/splitmix64.h`, `include/veque.h`.
- **Abandoned native-port stubs (broken, NOT in the build):**
  - `GeneralSimpifier.h` — a partial C++ port of GAMBA's `GeneralSimplifier`; references
    undefined `Node`/`NodeType`/`BasisExpression`/`IndexType`/`parse` and calls
    `vnumber()`/`get_basis_size()`/`get_variable_number()` as if they were functions.
    Does not compile.
  - `CSimplifyGeneral.h` — a `SimplifyGeneral` class declaration (references `Node`,
    `void *tree`).
  - `CSimplifyGeneral.cpp` — **empty (0 bytes)**.

### GAMBA (vendored, `external/GAMBA`, Python 3 — the reference oracle)
| File | Role |
|---|---|
| `src/simplify_general.py` | `GeneralSimplifier` — the **nonlinear** orchestrator. |
| `src/simplify.py` | `Simplifier` — the **linear** simplifier (original SiMBA; uses `BitwiseFactory`). |
| `src/utils/parse.py` | `Parser` + `parse()` — recursive-descent parser. |
| `src/utils/node.py` | `Node` AST + `NodeType`/`NodeState` + ~100 rewrite/refinement rules (~6650 lines). |
| `src/utils/batch.py` | `Batch`/`IndexWithMultitude` — sum factorization. |
| `src/utils/classify.py` | `Classifier` — statistics only. |
| `src/bitwise-factory/create_bitwise.py` | `BitwiseFactory` — truth-vector → bitwise expr (Quine–McCluskey + 1/2/3-var tables). |
| `src/bitwise-factory/utils/dnf.py` | `Dnf` — DNF construction / implicant merging. |
| `src/bitwise-factory/utils/implicant.py` | `Implicant` — conjunction vector, merge, hash. |
| `src/bitwise-factory/utils/bitwise.py` | `Bitwise` AST + `BitwiseType`, refine (insert-xor / flip-negation / extract). |
| `src/bitwise-factory/utils/bitwise_list_3vars.txt` | 3-var lookup table (8-bit truth rows). |
| `experiments/tests.py` + `experiments/datasets/` | Test harness (`ok/okz/z3/to/ng/nc/err`) incl. `mba_obf_nonlinear.txt`, `mba_flatten.txt`, `syntia.txt`, `qsynth_ea.txt`, `neureduce.txt`, `loki_tiny.txt`. |

GAMBA runtime deps: `numpy` (required), `z3` (optional), `gmpy2` (optional, for `popcount`).

---

## 2. Target architecture

New source directory `MBA/` (all in `namespace LSiMBA`), compiled into `LSiMBA++` and the
`SiMBA++`/`SiMBAPass` targets:

```
MBA/
  NodeType.h              NodeType + NodeState enums
  MBAValue.h              modular integer type + mod_red/popcount/trailing_zeros/power
  Parser.{h,cpp}          <- parse.py
  Node.{h,cpp}            <- node.py  (AST + all rewrite rules)   [largest piece]
  Batch.{h,cpp}           <- batch.py
  Bitwise.{h,cpp}         <- bitwise-factory/utils/bitwise.py
  Implicant.{h,cpp}       <- bitwise-factory/utils/implicant.py
  Dnf.{h,cpp}             <- bitwise-factory/utils/dnf.py
  BitwiseFactory.{h,cpp}  <- bitwise-factory/create_bitwise.py (+ 3-var table)
  LinearSimplifier.{h,cpp}<- simplify.py  (see §6 decision D1)
  GeneralSimplifier.{h,cpp}<- simplify_general.py  (replaces broken GeneralSimpifier.h)
  Classifier.{h,cpp}      <- classify.py  (optional, stats)
```

The existing native `Simplifier` (linear) stays as-is for the fast linear path. `GeneralSimplifier`
calls into `LinearSimplifier` for linear sub-expressions, mirroring how `simplify_general.py` calls
`simplify.py`.

---

## 3. Python → C++ mapping (file by file)

### 3.1 `parse.py` → `MBA/Parser`
Recursive-descent parser. Grammar precedence (low → high):
`|` (INCL_DISJUNCTION) → `^` (EXCL_DISJUNCTION) → `&` (CONJUNCTION) → `<<` (→ `a * 2**b`)
→ `+`/`-` (SUM) → `*` (PRODUCT) → unary `~` (NEGATION) / unary `-` (×−1) → `**` (POWER)
→ terminal (`(...)` / variable / constant).
- Constants: decimal, `0b…` binary, `0x…` hex.
- Variables: `[a-zA-Z][a-zA-Z0-9_]*`, optional `[digits]` suffix.
- Free function `parse(expr, bitCount, reduceConstants, refine, markLinear)`.
- `<<` desugars to `PRODUCT(base, POWER(2, rhs))`; nested `<<`/`**` require parentheses (error otherwise).

### 3.2 `node.py` → `MBA/Node`  (largest, highest-risk)
- Enums: `NodeType {CONSTANT, VARIABLE, POWER, NEGATION, PRODUCT, SUM, CONJUNCTION,
  EXCL_DISJUNCTION, INCL_DISJUNCTION}`; `NodeState {UNKNOWN, BITWISE, LINEAR, NONLINEAR, MIXED}`.
- `Node` fields: `type`, `children` (owned tree), `vname`, `vidx`, `constant`, `state`,
  `modulus`, `modRed`, `linearEnd`, `MAX_IT`.
- **Core (Phase 2):** `to_string(withParentheses, end, varNames)`, `eval`,
  `collect_and_enumerate_variables` / `collect_variables` / `enumerate_variables`,
  `get_max_vname`, `has_nonlinear_child`, `equals` / `equals_negated` /
  `__equals_rewriting_bitwise*`, `replace_variable(_by_constant)`, `copy` / `get_copy` /
  `__copy_all` / `__get_shallow_copy`, `__multiply` / `__multiply_by_minus_one`,
  `__get_opt_transformed_negated*`, `__add` / `__add_constant` / `__add_to_sum`,
  `mark_linear` (+ all `__mark_linear_*`, `__reorder_and_determine_linear_end*`),
  `count_nodes`, `compute_alternation(_linear)`, `count_terms_linear`, `sort` /
  `__reorder_variables` / `__lt`, `check_verify`, `print`.
- **Refinement (Phase 3):** `refine` = step 1 (`__inspect_constants*` per op, `__flatten*`,
  `__check_duplicate_children`, `__resolve_inverse_nodes*`, `__remove_trivial_nodes`) + step 2
  (~30 `__check_*` rules: nested-negation elimination, bitwise-negation transforms, power-of-two
  factoring, beautify-constants, move-in negations, xor-pair flips, power rewriting,
  product-of-powers, product-of-const-and-sum, factor-out-of-sum, inverse-negations-in-sum,
  insert-fixed-in-conj/disj, trivial-xor, xor-same-mult-by-minus-one, and the full conj/disj/xor
  identity-rule family). Plus `refine_after_substitution` (~13 `__check_*` rules) and `polish`
  (`__resolve_bitwise_negations_in_sums`, `__insert_bitwise_negations`, reorder).
- **Expansion & factorization (Phase 4):** `expand` / `__check_expand*` / `__expand_product` /
  `__multiply_sum*` / `__get_product*`; `factorize_sums` / `__collect_all_factors_of_sum` /
  `__node_from_batch` (uses `Batch`).
- **Substitution (Phase 5):** `get_node_for_substitution`, `substitute_all_occurences`,
  `__try_substitute_node`, `__try_substitute_part_of_sum*`, `replace_variable`.

### 3.3 `batch.py` → `MBA/Batch`
`IndexWithMultitude {idx, multitude}`; `Batch` (recursive partition of a sum into sub-batches +
atoms: `__partition`, `__get_next_batch`, `__collect_largest_batches`, `__reduce_multitudes*`,
`__get_largest_termset_indices`, `__check_for_nontrivial`, `is_trivial`, `print`).

### 3.4 bitwise-factory → `MBA/Bitwise`, `Implicant`, `Dnf`, `BitwiseFactory`
- `BitwiseType {TRUE, VARIABLE, CONJUNCTION, EXCL_DISJUNCTION, INCL_DISJUNCTION}`; `Bitwise`
  (negation as a flag, not a node) with `to_string`, `equals`, `refine`
  (`__check_insert_xor`, `__check_flip_negation`, `__check_extract`).
- `Implicant` (vec of 1/0/None, minterms, `try_merge`, `get_indifferent_hash`, `to_bitwise`, `get`).
- `Dnf` (group by popcount, Quine–McCluskey `__merge*`, `__drop_unrequired_implicants`, `to_bitwise`, `get`).
- `BitwiseFactory` (1/2/3-var tables; 3-var from `bitwise_list_3vars.txt`; `create_bitwise`
  with offset + negation; `__create_bitwise_with_offset` via `Dnf`).

### 3.5 `simplify.py` → `MBA/LinearSimplifier`  (see decision D1)
`Metric` enum; `Simplifier` (result-vector over truth-value combinations; `__simplify_generic`,
`__try_refine`, `__try_split`, `__try_simplify_fewer_variables`, decision-vector combinatorics
`__determine_comb_of_two*`, `__verify_using_z3`, `check_verify`). Free `simplify_linear_mba` +
`check_linear`.

### 3.6 `simplify_general.py` → `MBA/GeneralSimplifier`  (replaces broken `GeneralSimpifier.h`)
`GeneralSimplifier {bitCount, modulus, modRed, verifBitCount, vnumber, variables, MAX_IT=100,
VNAME_PREFIX="Y[", VNAME_SUFFIX="]"}`. Methods: `get_vname`, `mod_red`,
`collect_and_enumerate_variables`, `get_result_vector`, `get_groupsizes`,
`get_variable_combinations`, `get_basis_expression`, `are_variables_true`,
`subtract_coefficient`, `get_linear_combination`, `get_product_linear_combination`,
`get_power_linear_combination`, `get_occurring_variable_indices`, `try_simplify_sum_nonlinear_part`,
`is_candidate_for_simplification_in_sum`, `get_indices_of_simple_nonlinear_products_in_sum`,
`simplify_nonlinear_subexpression_linear_part`, `refactor`, `simplify_nonlinear_subexpression_step`,
`simplify_nonlinear_subexpression`, `simplify_linear_subexpression`,
`collect_nodes_for_substitution`, `get_simpl_via_substitution_of_nodes`,
`is_second_more_or_equally_complex`, `simplify_via_substitution_of_nodes`,
`simplify_via_substitution_for_index`, `simplify_via_substitution`, `simplify_subexpression`,
`verify_using_z3`, `get_variable_count`, `check_verify`, `simplify` (with timeout).
Free `simplify_mba(expr, bitCount, useZ3, modRed, verifBitCount)`.

### 3.7 `classify.py` → `MBA/Classifier`  (optional, statistics only)

---

## 4. Cross-cutting design decisions

### D1 — Linear simplifier: reuse vs. re-port
The repo already has a tested native linear `Simplifier`. **Decision:** port `simplify.py` as
`MBA/LinearSimplifier` so `GeneralSimplifier`'s linear sub-results are **byte-identical to
GAMBA** (required for differential testing), while keeping the existing `Simplifier` as the fast
standalone linear path. The two may share helpers. *(If exact parity proves unnecessary, fall back
to reusing the existing `Simplifier`; document the choice.)*

### D2 — Modular integer representation & the 2^64 problem
Python uses arbitrary-precision ints (`modulus = 2**bitCount`). For `bitCount == 64`,
`2**64` overflows a 64-bit type (the broken `GeneralSimpifier.h` used `int modulus` — a latent
bug). **Decision:** use `llvm::APInt` (or `__int128` where safe) for constants, coefficients, and
result vectors; `mod_red` must match Python's non-negative `%` semantics. `popcount`/
`trailing_zeros` via CPU intrinsics (replaces the optional `gmpy2`).

### D3 — Node ownership / copy semantics
Python `Node.copy` does `self.children = node.children` (aliases the **same** list), whereas
`__copy_all` deep-copies. This aliasing is subtle and load-bearing. **Decision:** own the tree with
`std::shared_ptr<Node>`; replicate `copy` (shallow/alias) vs `get_copy`/`__copy_all` (deep) exactly,
and add differential tests that specifically exercise aliased-child mutation.

### D4 — Expression string format (must be byte-identical for the oracle)
`to_string` parenthesization rules, `~`/`-` rendering, constant reduction
(`__get_reduced_constant_closer_to_zero` vs `modRed`), and `polish` ordering must match the Python
oracle exactly. The LLVM→string side (`getASTAsString`) emits `+ - * / % << >> ^ & |`; GAMBA's
parser supports `+ - * << & ^ | ~ ** 0b 0x` but **not** `>>`, `/`, `%`. For the native path the
AST is built in C++ directly (no string round-trip needed), so these operators can be handled natively;
only if we also keep the external path do we need to skip/transform `>>`/`/`/`%` MBAs.

### D5 — Error handling & timeouts
Python uses `sys.exit`/`assert` and a 30 s `multiprocessing` timeout in `simplify()`.
**Decision:** exceptions + `assert` (debug) in C++; implement the per-expression timeout as a
worker thread with a deadline (or rely on the `MAX_IT` iteration caps) returning an empty result on
timeout, matching GAMBA's `""` contract.

### D6 — Determinism
Variable enumeration order (`sort` by `(len, name)`), `__lt` ordering, and stable iteration must be
reproduced so outputs are reproducible and diff-able against the oracle.

---

## 5. Phased execution plan (each phase gates on differential tests vs. the Python oracle)

> **Differential harness (Phase 0, used by every later phase):** a driver that, for a corpus of
> expressions, runs the C++ (via a small `mba-cli` or a thin binding) and the Python GAMBA and
> diffs `to_string` / simplified output / verification result. Corpora: the `experiments/datasets/*`
> files, `data/*` MBAs, and generated random expressions. The vendored Python GAMBA is the source of
> truth and is **not** deleted.

- **Phase 0 — Foundation & harness.** Create `MBA/`; add sources to `CMakeLists.txt` (extend
  `LSiMBA++`, `SiMBA++`, `SiMBAPass`); define `NodeType`/`NodeState`, `MBAValue` (D2),
  `mod_red`/`popcount`/`trailing_zeros`/`power`; build the differential harness + a `mba-cli`
  debug front-end. *Accept:* compiles clean; harness round-trips a few expressions.
- **Phase 1 — Parser.** Port `parse.py`. *Accept:* `parse(e).to_string()` == Python for the corpus;
  invalid inputs error identically.
- **Phase 2 — Node core (no rewrite rules).** Port fields + core methods (§3.2 core). *Accept:*
  `eval`, `mark_linear` states, `to_string`, `equals`/`equals_negated`, `check_verify` all match
  Python on the corpus.
- **Phase 3 — Node refinement (rewrite rules).** Port `refine` (step 1 + ~30 step-2 rules),
  `refine_after_substitution`, `polish`. Do it in rule-family sub-steps (constants/flatten →
  bitwise-negation → power-of-two → product/power resolution → sum-merge → bitwise-in-sum →
  bitwise-pair merges → identity rules → substitution cleanup). *Accept:* `refine()` output matches
  Python on the full corpus; `check_verify` passes; no rule regresses earlier phases.
- **Phase 4 — Expansion & factorization.** Port `expand`/`__expand_*` and `factorize_sums` + `Batch`.
  *Accept:* `expand`/`factorize_sums` outputs match Python.
- **Phase 5 — Substitution.** Port the substitution methods (§3.2). *Accept:* substitution results
  match Python.
- **Phase 6 — BitwiseFactory.** Port `Bitwise`/`Implicant`/`Dnf`/`BitwiseFactory` (+ 3-var table).
  *Accept:* `create_bitwise(vnumber, vec, offset, vars)` matches Python for all 1/2/3-var truth vectors.
- **Phase 7 — LinearSimplifier.** Per D1, port `simplify.py`. *Accept:* `simplify_linear_mba`
  matches Python `simplify.py` output.
- **Phase 8 — GeneralSimplifier.** Port `simplify_general.py` (replace `GeneralSimpifier.h`); wire it
  to `LinearSimplifier` + `Node` + Z3. *Accept:* `GeneralSimplifier.simplify(e)` matches Python
  `simplify_general.py` on the full nonlinear datasets; Z3/eval verification passes.
- **Phase 9 — Integration into CLI & LLVM pass.** Add `--simplifier {native|general|external}`;
  route linear → native `Simplifier`, nonlinear → `GeneralSimplifier`; make `--mba`/`--mbadb`/`--ir`
  all honor it; add missing options (`--max-var-count`, `--min-ast-size`, `--walk-sub-ast`,
  `--print-smt`, `--timeout`, `--accept-unknown`). *Accept:* end-to-end verified simplification of
  nonlinear MBAs on raw strings, databases, and LLVM IR.
- **Phase 10 — Testing, CI, cleanup.** CTest entries; CI job (no Python required for the core; keep
  an optional differential job that uses the vendored GAMBA); delete/archive the broken
  `GeneralSimpifier.h`/`CSimplifyGeneral.*` once replaced; update `README.md`. *Accept:* nonlinear
  solve rate comparable to published GAMBA with `err == 0`; linear results unchanged vs. baseline.

---

## 6. Open decisions (confirm before Phase 0)
- **D1** — re-port `simplify.py` for exact GAMBA parity (recommended) vs. reuse existing `Simplifier`.
- **D2** — `llvm::APInt` vs. `__int128` for modular values.
- **D3** — `std::shared_ptr<Node>` tree with replicated shallow/deep copy semantics.
- **D7** — keep `external_simplifier` (Python GAMBA) as a fallback/cross-check option alongside the
  native `GeneralSimplifier` (recommended: yes, for validation during rollout).
- **D8** — C++ standard: keep C++17 (project baseline).

---

## 7. Risks & mitigations
| Risk | Impact | Mitigation |
|---|---|---|
| `node.py` ~100 rewrite rules; subtle aliasing (`copy` vs `__copy_all`) | Wrong/inequivalent output | Phase-gated differential testing vs. Python oracle; dedicated aliasing tests (D3); `check_verify` + Z3 as a backstop on every result. |
| 2^64 overflow in `modulus`/constants | Silent corruption at 64-bit | `APInt`/128-bit (D2); match Python non-negative `%`; 64-bit-specific corpus cases. |
| `to_string`/`polish` formatting drift | Differential tests fail spuriously | Byte-identical formatting requirement (D4); keep oracle as source of truth. |
| Performance (C++ should win, but `MAX_IT` caps + substitution search can be slow) | Timeouts on hard MBAs | Worker-thread timeout (D5); keep `MAX_IT` caps; measure vs. Python per phase. |
| Scope creep / nonlinearity of the port | Schedule | Strict phase gates; each phase independently testable; keep external path (D7) as a safety net. |

## 8. Acceptance criteria (overall)
1. `SiMBA++ --mba "<nonlinear MBA>" --simplifier general` and `--ir <module>` produce verified
   simplifications for nonlinear MBAs that the native linear path cannot.
2. Every replaced MBA passes fast-check (and Z3 when enabled) — never trust an unverified result.
3. Linear-MBA results are unchanged vs. the current baseline (no regression).
4. A CI run over `mba_obf_nonlinear.txt` / `mba_flatten.txt` / `syntia.txt` reports a solve rate
   comparable to the published GAMBA numbers with `err == 0`.
5. Differential tests vs. the vendored Python GAMBA pass for `parse`, `refine`, `expand`,
   `factorize_sums`, substitution, `BitwiseFactory`, and `GeneralSimplifier.simplify`.

## 9. Immediate next steps
1. Confirm open decisions D1–D8 (above).
2. Scaffold `MBA/` + CMake + `MBAValue`/enums (Phase 0).
3. Build the differential harness + `mba-cli` front-end.
4. Begin Phase 1 (Parser) and gate on the oracle.

---

## 10. Progress Status & Remaining Work (updated)

### Phases 0–9: COMPLETE
All native port files exist under `MBA/` and are wired into `LSiMBA++`, `SiMBA++`, and
`SiMBAPass` (CMake). Phase 9 CLI/LLVM integration is done: `--simplifier {native|general|external|auto}`
plus `--max-var-count`, `--min-ast-size`, `--walk-sub-ast`, `--timeout`, `--print-smt`,
`--accept-unknown` are all present and honored by `--mba`.

- **64-bit correctness (D2):** fixed. Every `2^bitCount` / modulo site special-cases `bitCount>=64`
  (`MBAOps::reduce` / `MBAOps::widthMask` instead of the UB `1ULL << bitCount`). Verified: basic
  nonlinear cases (`x*y+y*y`→`y*(x+y)`, `x*y+x*z`→`x*(y+z)`, …) match the Python oracle at 64-bit,
  end-to-end through `SiMBA++.exe` (tob LLVM 20.1 + Z3).
- **Differential suite** (`MBA/run_all_tests.py`): all 9 pass (`parse`, `parse_dataset`, `node`,
  `refine`, `phase4_expand_factorize`, `subst`, `bitwise`, `simplify_linear`, `general`).
- **Phase 10 (partial):** CTest fragment + CI `mba_differential` job added; README updated; broken
  stubs (`GeneralSimpifier.h`, `CSimplifyGeneral.*`) archived to `_deprecated/`.

### Remaining work (blocks §8 acceptance)

1. **[FIXED] Nonlinear-simplifier correctness bug.** `general "x + x*y"` returned `x*(y)` (wrong;
   oracle → `-x*~y`) and hung at 8-bit; also wrong on `y + x*y + y*y` and `2*y + y*y`.
   Two distinct root causes were found and fixed:
   - **64-bit signed-residue bug** in `Node::resolveBitwiseNegationsInSums` (RefineD.cpp):
     `negConst = modRedValue(-1, 64)` returned the signed `-1` instead of the non-negative residue
     `2^64-1`, so the `children.size() < negConst` / `countM < negConst` early-returns never fired and
     the constant term was **unconditionally erased** from sums like `1+y` (→ `y`), corrupting
     `(1+y)*x` → `x*y`. Fixed by computing `negConst` with `MBAOps::reduce((uint64_t)(-constLow), bitCount)`
     (uint64 residue) and using unsigned comparisons.
   - **Iterator-invalidation bug** in `Node::polish` (RefineD.cpp): the child loop
     `for (auto &c : children) c->polish(this)` iterates the parent's `children` vector, but a child's
     `polish` calls `insertBitwiseNegations`, which does `parent->multiply(factor)` and **inserts a
     constant into the parent's `children` mid-iteration** — invalidating the range-for iterator
     (undefined behavior → hang on `x + x*y`). Fixed by iterating over a copy of `children`.
   Verified: `x + x*y`→`-x*~y`, `y + x*y + y*y`→`-y*~(x+y)`, `2*y + y*y`→`y*(2+y)` at **8- and 64-bit**,
   all matching the Python oracle; no hang. Full differential suite still all-9 PASS.
 2. **[FIXED] Verification not enforced on the general path (AC2).** `--mba <expr> --simplifier general`
    printed no fast-check/prove line and trusted the result blindly. Two root causes, both fixed:
    - **Fast-check was never wired in**: `RouteSimplify` (SimplifierRouter.cpp) discarded the
      `fastCheck` argument (`(void)fastCheck;`), so general/external results were reported valid
      with no random-value verification. Fixed: new `MBA/Verify.cpp` - `fastCheckEquivalent`
      compares original vs result on 100 deterministic splitmix64 random assignments via the GAMBA
      evaluator (modular `Node::eval`, 8- and 64-bit) - and `RouteSimplify` now runs it on every
      non-native result (incl. the `--walk-sub-ast` fallback); a counterexample yields the new
      `RouteResult::INVALID` and `--mba`/`--mbadb` report "Not valid replacement! (verification
      failed)" instead of success.
    - **Z3 prove was a stub**: `GeneralSimplifier::verifyUsingZ3` returned `orig == simpl`
      (syntactic identity), so `--prove` neither proved anything nor could ever accept a real
      simplification. Fixed: it now calls `proveEquivalent` (Verify.cpp), which runs the
      project's real Z3 backend (`proveReplacement`, Z3Prover.cpp) on the expression (0x/0b
      constants normalized to decimal for the native tokenizer).
    Verified: `mba_cli verify` accepts equivalent and rejects known-wrong pairs at 8/64-bit (incl.
    the `1+y` vs `y` regression from item 1, printing a counterexample); `SiMBA++.exe --mba "x + x*y"
    --simplifier general` passes the built-in fast-check; `--prove` runs the real Z3 proof (284 ms
    on the same case). Full differential suite all-9 PASS; `--ir` (AC1) and the native baseline
    unchanged.
3. **[FIXED] `--ir <module>` path crashed in Z3 (AC1) - build was linking the wrong Z3 (c:\libs v4.12.1.0, not tob v5.0.0.0).**
   `SiMBA++.exe --ir llvm/MBA_OP.ll --bitcount 32` (native **and** `--simplifier general`, even with
   `--fastcheck=false --prove=false`) aborts in `libz3.dll` at `Z3_model_get_func_decl`
   (`0xC0000005`). The `--mba` path is clean; the `--ir`/`LLVMParser` path hits a Z3 model-extraction
   fault. Investigate the Z3 solver lifecycle in `LLVMParser.cpp` / `Z3Prover.cpp` (solver cache /
   `resetZ3Solver`) and the tob Z3 header-vs-DLL match. **Root cause (no source change):** `build/CMakeCache.txt` had `Z3_DIR=C:/Libs/lib/cmake/z3` — a broken
Z3 4.12.1.0 install whose `z3::libz3` is a STATIC IMPORTED target with **no `IMPORTED_LOCATION`** (links
no library), so the exe bound a stale/mismatched `libz3` at runtime and faulted on model extraction. The
working reference build (`G:\saturn_windows\build_SiMBA++\Simba-`) uses the tob Z3
(`G:/saturn_windows/deps/tob_libraries/z3/lib/cmake/z3`, **v5.0.0.0**). Reconfigured `build/` with
`-DZ3_DIR=…/tob_libraries/z3/lib/cmake/z3` (+ matching `Z3_INCLUDE_DIRS`/`Z3_LIBRARIES`, tob LLVM) and
rebuilt. Verified: `--ir MBA_OP.ll` / `mbas.ll` (native + general) exit 0; `--mba "a^b" --prove` (the
user's repro) runs clean; `--mba … --simplifier general` still correct.
4. **[FIXED] CI dataset solve-rate benchmark (AC4).** Added `MBA/solve_rate.py` (runs the C++
   `general` CLI over `mba_obf_nonlinear.txt` / `mba_flatten.txt` / `syntia.txt`, reports the
   solve-rate = fraction solved within the per-expression timeout) and a CI step in
   `.github/workflows/cmake.yml` (`mba_differential` job). Measured: 100% / 100% / 97.5% at 8-bit.
5. **[DONE] Cleanup.** Deleted temp `MBA/bisect_general.py`, `MBA/investigate_general.py`, all
   `MBA_TRACE` debug traces, and the temp batch/output files.
