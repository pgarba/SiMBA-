# Fix plan: 6 `qsynth_ea` expressions where the C++ port returns non-equivalent results

Self-contained plan for a fresh session. All paths relative to `C:\github\SiMBA-`
(branch `feat/gamba-native-verification`, pushed to origin).

## STATUS: FIXED

Root cause: `LinearSimplifier::partition` took its `lrem` argument **by value**, while
the Python oracle's `__partition` mutates the list **in place**. Terms the partitioner
could not place into a disjoint partition were appended to a *copy* the caller never
saw, so `simplifyPartsAndCompose` composed only the original `lrem` and silently dropped
the rest — a confidently-wrong, "simpler" result.

Fix: `partition` now takes `lrem` by non-const reference (`std::vector<int> &lrem`),
mirroring the Python list-by-reference semantics (single call site: `trySplit`).

Verified: all 6 cases now verify equivalent; benchmark 83/100 solved, 83/100 valid
(was 77/100 valid), solve rate unchanged. Regression guard `MBA/diff_qsynth_ea.py`
wired into `MBA/run_all_tests.py`. All temporary instrumentation removed.

## 1. Symptoms (established facts)

- The benchmark (`MBA/BENCHMARK_PLAN.md`, 8-bit, first 100 lines of
  `external/GAMBA/experiments/datasets/qsynth_ea.txt`) fast-checks every result
  against the dataset ground truth. 6 results are **refuted** (indices
  3, 52, 88, 91, 93, 98). All other 7 datasets: 100/100 valid.
- The 6 failing `expr,groundtruth` lines are pre-extracted in
  **`MBA/qsynth_ea_failing.txt`**.
- The dataset is consistent: for all 6, `verify <expr> <groundtruth>` =
  EQUIVALENT. The port's results are not (e.g. index 3, a non-constant
  expression, is returned as the constant `-1`; the Python oracle returns
  `~(d^c+d)-(c^b*d)` for it).
- Fails at **8-bit and 64-bit** alike → logic bug, not a width/UB bug.
- All 6 expressions contain `<<` (the parser desugars `a << b` to
  `a * 2**b`) and have 3-4 variables.
- The 3-dataset differential suite (`MBA/diff_general.py`) does not cover
  qsynth_ea — that is why this went undetected.

Reproduce:

```
python MBA\_dbg_qsynth.py            # per-case counterexamples
python MBA\bench_compare.py 100 8    # full benchmark (needs sandbox escalation, see §6)
```

## 2. Stage bisect (already done — case index 3, 8-bit)

`mba_cli <mode> 8 <expr>` then `mba_cli verify 8 <expr> <result>`:

| mode | equivalent? |
|---|---|
| parse | OK |
| refine | OK |
| polish | OK |
| expand | OK |
| factorize | OK |
| general | **FAIL** (returns `-1`) |

(`subst` mode "fails" by design — its debug output introduces a fresh variable
`t`; ignore it, but note the substituted node it printed:
`(...|~c)*2**1` — i.e. the substitution target **contains the desugared `<<`**.)

## 3. Root-cause analysis (hypotheses, ranked)

Key code locations:

- `MBA/GeneralSimplifier.cpp`
  - `simplify` (end of file) — no final equivalence check at all.
  - `simplifyViaSubstitution` (line 625), `simplifyViaSubstitutionForIndex`
    (604), `simplifyViaSubstitutionOfNodes` (585),
    `getSimplViaSubstitutionOfNodes` (541).
  - **`simplifyViaSubstitutionOfNodes` accepts a substituted tree on
    `isSecondMoreOrEquallyComplex` (complexity heuristic) alone — no
    equivalence gate.**
- `MBA/Substitute.cpp` — `substituteAllOccurences`, `replaceVariable`,
  `getNodeForSubstitution`.
- `MBA/Verify.cpp` — `fastCheckEquivalent` (100-sample gate; reusable for
  instrumentation).
- Python oracle: `external/GAMBA/src/simplify_general.py`
  - line 524 `__simplify_via_substitution_of_nodes` — **identical acceptance
    logic (complexity only, no equivalence gate)**.
  - line 626 `__check_verify` — the final check is **opt-in and OFF by
    default** (`if self.__verifBitCount == None: return True`).
  - line 673 `simplify_mba` — returns the result only if `__check_verify`
    passes (which is always True without `-v`).
  - `external/GAMBA/src/utils/node.py` `check_verify` — **exhaustive**
    enumeration of all assignments (default verify bitCount=2).

Since the substitution *acceptance* logic is identical in both
implementations, the divergence must be in a lower-level primitive. Ranked:

1. **`Substitute.cpp` vs `node.py` `substitute_all_occurences` /
   `replace_variable`** — traversal/matching bugs when the tree contains the
   desugared `**` (`<<` → `x * 2**1`): e.g. matching a node that is a
   sub-part of a `POWER` base/exponent, or `replaceVariable` corrupting a
   `POWER` subtree. All 6 cases contain `<<`; the case-3 substitution target
   was `(...|~c)*2**1`.
2. A refinement/polish rule misbehaving on a *substituted* tree (a `Y[`
   temp variable inside a `**`), i.e. `refine()`/`polish()` after
   `replaceVariable` — compare rule-by-rule against `node.py`
   `refine`/`polish` on the same tree.
3. `getNodeForSubstitution` / `collectNodesForSubstitution` selecting a node
   whose substitution is unsafe with `**` (the Python `get_node_for_substitution`
   in node.py may filter such nodes differently).

## 4. Fix plan (step by step)

1. **Instrument** (temporary): in `GeneralSimplifier::simplifySubexpression`
   and `simplifyViaSubstitutionOfNodes`, after each accepted sub-step verify
   the current tree against the original expression (exact, cheap for 3-4
   vars: exhaustive 2-bit enumeration like the oracle's `check_verify`, or
   `fastCheckEquivalent` at the expression bitCount). Run case index 3 from
   `MBA/qsynth_ea_failing.txt` until the first failing step is identified.
2. **Differential-bisect that step** against the Python oracle: feed the same
   intermediate tree to the corresponding Python function
   (`_py_general.py` imports the oracle module — extend it to call
   `substitute_all_occurences` / `replace_variable` / `refine` directly) and
   diff the resulting tree strings.
3. **Fix the rule** in C++ (mirror the Python semantics exactly; keep the
   project's 64-bit rule: never `1ULL << 64`, use `MBAOps::reduce` /
   `widthMask`).
4. **Parity hardening** (optional but recommended): implement the oracle's
   opt-in final check in C++ `simplify` — when `verifyBitCount > 0`, run the
   exhaustive `check_verify`-style evaluation and return `""` on mismatch
   (mirrors `simplify_general.py` line 626/673; off by default, like the
   oracle).
5. **Remove instrumentation.**

## 5. Acceptance criteria

- `python MBA\_dbg_qsynth.py` → **zero** invalid cases (target: all 6
  solved **and** valid, matching the oracle).
- Re-run `python MBA\bench_compare.py 100 8` → qsynth_ea: `valid == solved`,
  no refuted results; update the README benchmark table + `MBA/bench_results.csv`.
- **Add qsynth_ea as a 10th dataset** to `MBA/diff_general.py` `DATASETS`
  (regression guard — its absence is how this slipped through).
- `python MBA\run_all_tests.py 100` → all suites PASS; solve-rate on the
  original 3 datasets unchanged (100/100/97 at 8-bit); native linear baseline
  (`SiMBA++.exe --mba "a^b" --prove`) unchanged.
- Update `GAMBA_INTEGRATION_PLAN.md` item 6 → **[FIXED]** with the root
  cause; commit + push to `feat/gamba-native-verification`.

## 6. Environment notes (Windows, no network)

- Rebuild `mba_cli`: `powershell -File MBA\build.ps1` (after any `MBA/*.cpp`
  change). Full `SiMBA++`: vcvarsall x64 →
  `cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DLLVM_DIR=G:/saturn_windows/deps/tob_libraries/llvm/lib/cmake/llvm/ -DZ3_DIR=G:/saturn_windows/deps/tob_libraries/z3/lib/cmake/z3 -DZ3_INCLUDE_DIRS=G:/saturn_windows/deps/tob_libraries/z3/include/ -DZ3_LIBRARIES=G:/saturn_windows/deps/tob_libraries/z3/lib/libz3.lib -S . -B build` → `nmake` in `build/`.
- The **real** Python oracle (`simplify_general.py`) uses multiprocessing
  (Windows named pipes) → needs a sandbox escalation
  (`sandbox_permissions: danger-full-access`); the wrapper `MBA/_py_general.py`
  (stubbed numpy + synchronous process) works under workspace-write.
- Oracle CLI: feed the expression via **stdin** (`python simplify_general.py -b 8`
  with the expression on stdin) — argparse treats a leading `-` as an option.
  Result line: `*** ... simplified to <simpl>`.
- `GAMBA_INTEGRATION_PLAN.md` and `README.md` are UTF-8; for programmatic
  edits use `[System.IO.File]::ReadAllText/AppendAllText` with explicit UTF-8
  (no BOM), not `Get-Content`/`Add-Content`.
- Semantics: mod 2^bitCount, non-negative residues; Python `%` is always
  non-negative. Correctness criterion = semantic equivalence, not
  byte-identical output.

## 7. Files created by this investigation (keep)

- `MBA/qsynth_ea_failing.txt` — the 6 `expr,groundtruth` lines.
- `MBA/_dbg_qsynth.py` — per-case counterexample printer (imports
  `bench_compare`).
- `MBA/_extract_fail.py` — re-extracts the failing cases + stage bisects
  case 3 (re-runnable after the fix; the stage table should then show
  `general` OK).
