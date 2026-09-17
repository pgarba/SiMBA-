# SiMBA++

```
   _____ __  ______  ___    __    __
  / __(_)  |/  / _ )/ _ |__/ /___/ /_
 _\ \/ / /|_/ / _  / __ /_  __/_  __/
/___/_/_/  /_/____/_/ |_|/_/   /_/v2.0
°°SiMBA ported to C/C++/LLVM ~pgarba~
```

**SiMBA** (Tool for the simplification of linear mixed Boolean-arithmetic expressions (MBAs)) ported to **C/C++** with some enhancements and multithreading support. 

* **Able to directly work on LLVM IR!**
* **Compiles as standlone tool and plug-in for Clang/Opt**


Ported from:
https://github.com/DenuvoSoftwareSolutions/SiMBA

# Multithreading

MBA evaluation and verification will be run in parallel, if the MBA has more than 3 variables.

Not supported in LLVM, as LLVM does not support multithreading!

# Works with external simplifiers like SiMBA/GAMBA

Use an external simplfier like **GAMBA** to crunch non linear MBAs on LLVM IR!

# Native C++ GAMBA port (nonlinear MBAs)

The `MBA/` directory contains a native C++ port of the **GAMBA** nonlinear
MBA simplifier (the vendored Python/C# reference was removed once the port
was complete and validated), which extends SiMBA's linear MBA simplification
to **nonlinear** MBAs. It is built and verified independently of the main LLVM
build:

- **Build the standalone MBA core + CLI:** `powershell -File MBA\build.ps1`
  (Windows; produces `MBA\build\mba_cli.exe`) or `bash MBA/build.sh` (Linux,
  needs `llvm-config`).
- **Run the differential tests** (C++ port vs the vendored Python GAMBA oracle):
  `python MBA\run_all_tests.py 100`.
- **Solve-rate benchmark** over the GAMBA datasets:
  `python MBA/solve_rate.py 100 8`.

The port covers the parser, node core, refinement rules, expansion/factorization,
substitution, the bitwise factory, the linear simplifier, and the general
(nonlinear) simplifier. See `plans/GAMBA_INTEGRATION_PLAN.md` for the full phase-by-phase
plan and verification status.

The `SiMBA++` CLI routes simplification through
`--simplifier {native|general|external|msimba|auto}` (default `auto`: linear
MBAs go to the native path, semi-linear ones to the MSiMBA port, and
nonlinear ones to the native GAMBA port).

**Auto-fallback (default on, `--auto-fallback`; disable with
`--auto-fallback=false`):** in `auto` mode, if the chosen route produces no
result, the remaining routes are tried in order (msimba → general → external)
before giving up. This covers the case where `checkLinear` classifies an
expression as linear but the native simplifier cannot reduce it, while
MSiMBA/general can. Explicit `--simplifier=X` stays single-route (no
fallback).

**Verification is enforced on every non-native result** ("never trust an
unverified result"):

- **Fast-check (default, `--fastcheck`):** the original expression and the
  result are compared on 100 deterministic random assignments using the GAMBA
  evaluator (modular 2^bitCount semantics, 8- and 64-bit). A counterexample
  prints the offending values and the result is reported as
  `Not valid replacement! (verification failed)` instead of a success.
- **Z3 proof (`--prove`):** the result is additionally proved equivalent to the
  original with the project's Z3 backend (`MBA/Verify.cpp` → `proveReplacement`).

The standalone `mba_cli` exposes the same checks directly:
`mba_cli verify <bitCount> <orig> <simp>` (fast-check) and
`mba_cli prove <bitCount> <orig> <simp>` (Z3; the standalone build links no
Z3, so `prove` is a no-op there — use `SiMBA++.exe --prove` for real proofs).

# Native C++ MSiMBA port (semi-linear MBAs)

The `MBA/` directory also contains a native C++ port of the **MSiMBA**
multi-bit algorithm (arXiv:2406.10016), which extends SiMBA's linear MBA
simplification to **semi-linear** MBAs — expressions with constants inside
bitwise operands (e.g. `x ^ 5148131303079159687`, `~(C | y)`). These are the
MBAs that appear in real-world obfuscated code after constant propagation.

Unlike the GAMBA general solver (exponential in the variable count), the MSiMBA
algorithm is **polynomial** — O(2^t × N) for the signature vector, where t is
the variable count and N the bit width — so it solves 64-bit, 6-variable
semi-linear MBAs in well under a second per expression.

- **Build:** part of the main CMake build (`mba_cli` target); no separate step.
- **CLI:** `mba_cli msimba <bitCount> <expr>` (simplify) and
  `mba_cli msimbacheck <bitCount> <expr>` (linearity check).
- **Routing:** the `SiMBA++` CLI auto-detects semi-linear MBAs and routes them
  to the MSiMBA path (`--simplifier=msimba` forces it; `--simplifier=auto`
  sends linear MBAs to the native path, semi-linear to MSiMBA, and nonlinear
  to the GAMBA port).
- **Verification:** every MSiMBA result is fast-checked against the original
  before being accepted (the verification gate rejects non-equivalent results).

The port covers the signature vector, the linearity check, the initial linear
combination, the multi-bit refiner, the constant substituter, the XOR recovery
(constant offset, coefficient normalization, top-bit normalization, 3-term XOR
pattern), and the GT term ordering (primary-variable rule + alphabetical sort).
See `plans/MSIMBA_INTEGRATION_PLAN.md` and `plans/MSIMBA_100_PERCENT_PLAN.md`
for the full phase-by-phase plan and verification status.

# Missing-operator support (`>>`, `/`, `%`)

The GAMBA expression language now supports `>>` (shift right), `/` (integer
divide) and `%` (integer remainder) — see
`plans/GAMBA_MISSING_OPERATORS_PLAN.md`. Two tiers:

- **Tier 1 (bit desugaring):** `var >> k`, `var / 2^k` and `var % 2^k` are
  desugared at parse time into a sum of bit-slice terms (`a[i] * 2^i`), which
  the simplifier handles as a linear expression. In the native port, anything
  else (compound LHS, non-power-of-two or non-constant divisor) builds a
  first-class Tier 2 operator node instead of erroring (only a constant divisor
  of zero is still a parse error). The vendored oracle still rejects those cases.
- **Tier 2 (first-class nodes, native port only):** `RSHIFT`/`UDIV`/`UREM`
  operator nodes with exact unsigned semantics, so variable divisors and
  non-power-of-two divisors parse and evaluate. They are marked nonlinear and
  the general simplifier treats them as **opaque leaves** (children simplified,
  the operator node itself never rewritten or reordered — the operators are not
  commutative). The vendored Python oracle is **not** modified for Tier 2 (it
  still rejects the non-desugarable cases), so the differential suite is not
  extended with Tier 2 cases. Verified natively via `MBA/test_tier2_semantics.py`
  (4000 checks @ 8-bit, 1600 @ 16-bit, 0 MISMATCH).

**Semantics note.** All three operators use **unsigned** integer semantics over
values reduced mod `2^B` (floor division for `/`, remainder for `%`).
`LLVMParser::getASTAsString` emits `>>` for both `LShr` and `AShr`, `/` for
both `UDiv` and `SDiv`, and `%` for both `URem` and `SRem`. **`AShr` is
excluded from the routed string path** (an arithmetic shift is not expressible
as a logical shift, so it falls back to the native path). The `SDiv`/`SRem` →
`/`/`%` mapping assumes unsigned semantics (signed division rounds toward zero;
signed remainder carries the dividend's sign) — the fast-check gate catches any
mismatch on the routed path.

**Performance note.** The bit-desugaring produces one term per bit-sliced
variable, and the GAMBA simplifier (Python oracle *and* native port) is
**exponential** in that count, so 8–16-bit values simplify quickly but
32/64-bit desugared values are impractically slow. Tier 2 avoids the
bit-explosion for the general cases.

# GAMBA benchmark (Python oracle vs. C++ port)

For each test file (first 100 expressions, 8-bit, fresh process per
expression) the implementations simplify all expressions and every result is
fast-checked against the dataset's `groundtruth` column. The first block is the
seven vendored GAMBA datasets (methodology: `plans/BENCHMARK_PLAN.md`; reproduce
with `python MBA\bench_compare.py 100 8`, raw numbers: `MBA/bench_results.csv`);
the second block is the `data/` test files, benchmarking the SiMBA++ native
linear simplifier against the GAMBA port (reproduce with
`python MBA\bench_simba_data.py 100 8`, raw numbers: `MBA/bench_simba_data.csv`).

| test file | C++ before fix (s) | C++ port (s) | C++ valid | Python GAMBA (s) | Python valid | speedup | SiMBA++ (s) | SiMBA++ valid |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| mba_obf_nonlinear | 2.1 | 2.0 | 100/100 | 86.4 | 100/100 | 43.2x | — | — |
| mba_flatten | 2.1 | 1.9 | 100/100 | 85.5 | 100/100 | 45.0x | — | — |
| syntia | 21.1 | 1.9 | 100/100 | 83.6 | 100/100 | 44.0x | — | — |
| mba_obf_linear | 2.1 | 2.0 | 100/100 | 85.6 | 100/100 | 42.8x | — | — |
| qsynth_ea | 110.8 | 4.0 | 100/100 | 92.0 | 100/100 | 23.0x | — | — |
| neureduce | 2.1 | 1.9 | 100/100 | 85.9 | 100/100 | 45.2x | — | — |
| loki_tiny | 2.0 | 1.9 | 100/100 | 85.3 | 100/100 | 44.9x | — | — |
| e1_2vars | — | 1.8 | 100/100 | — | — | — | 1.7 | 100/100 |
| e1_3vars | — | 1.9 | 100/100 | — | — | — | 1.7 | 100/100 |
| e1_4vars | — | 3.6 | 100/100 | — | — | — | 1.9 | 100/100 |
| e1_5vars | — | 11.9 | 100/100 | — | — | — | 2.6 | 100/100 |
| e1_6vars | — | 88.4 | 100/100 | — | — | — | 6.6 | 100/100 |
| e2_2vars | — | 1.8 | 100/100 | — | — | — | 1.7 | 100/100 |
| e2_3vars | — | 1.9 | 100/100 | — | — | — | 1.7 | 100/100 |
| e2_4vars | — | 3.6 | 100/100 | — | — | — | 1.9 | 100/100 |
| e3_2vars | — | 1.8 | 100/100 | — | — | — | 1.7 | 100/100 |
| e3_3vars | — | 1.9 | 100/100 | — | — | — | 1.7 | 100/100 |
| e3_4vars | — | 3.7 | 100/100 | — | — | — | 1.9 | 100/100 |
| e4_2vars | — | 1.9 | 100/100 | — | — | — | 1.7 | 100/100 |
| e4_3vars | — | 2.0 | 100/100 | — | — | — | 1.7 | 100/100 |
| e4_4vars | — | 3.7 | 100/100 | — | — | — | 1.9 | 100/100 |
| e5_2vars | — | 1.8 | 100/100 | — | — | — | 1.7 | 100/100 |
| e5_3vars | — | 1.9 | 100/100 | — | — | — | 1.7 | 100/100 |
| e5_4vars | — | 3.7 | 100/100 | — | — | — | 1.9 | 100/100 |
| pldi_linear | — | 1.8 | 100/100 | — | — | — | 1.7 | 100/100 |
| pldi_poly | — | 1.8 | 100/100 | — | — | — | 1.6 | 0/100 |
| pldi_nonpoly | — | 1.9 | 100/100 | — | — | — | 1.6 | 0/100 |
| test_data | — | 1.8 | 100/100 | — | — | — | 1.7 | 100/100 |
| mbablast_ds1 | — | 0.9 | 53/53 | — | — | — | 0.8 | 53/53 |
| mbablast_ds2_8 | — | 1.7 | 100/100 | — | — | — | 1.5 | 100/100 |

`SiMBA++ (s)`/`SiMBA++ valid` are the native SiMBA++ **linear** simplifier
(`--simplifier=native`); `C++ port` is the GAMBA native port. The port solves
every `data/` file 100/100 — including the nonlinear `pldi_poly`/`pldi_nonpoly`,
which the linear simplifier cannot solve (0/100) — so the port extends SiMBA++
to nonlinear MBAs. On the linear files the two are comparable, and the native is
faster on the high-variance files (e.g. `e1_6vars`: 6.6 s vs 88.4 s).
`mbablast_ds1` has 53 expressions.

`C++ port (s)` is the current (post-fix) time; `C++ before fix (s)` is the
time before the correctness pass (see below). The pass removed the bad
rewrites that made the two hardest datasets run to their per-expression
deadline: **qsynth_ea went from 110.8 s (83/100 solved) to 4.0 s (100/100)
— a ~28x speedup** — and **syntia from 21.1 s (97/100) to 1.9 s (100/100),
~11x**. The remaining datasets were already fast and are unchanged within
run-to-run noise.

"valid" = solved and fast-check equivalent to the ground truth (the ground
truth is a correct simplification of the original, so the two checks agree).
Python times include ~0.85 s of per-expression interpreter start-up (numpy
import); the C++ binary start-up is ~ms.

- `syntia`: all 100 expressions now solve (the 3 that previously hit the
  25 s deadline were non-converging rewrites removed by the correctness
  pass below).
- `qsynth_ea`: the port is now fast here (100/100 solved in 4.0 s, 23.0x
  speedup; previously 17/100 hit the 25 s deadline — 110.8 s total, 0.8x,
  83/100 valid). Two former correctness bugs on these inputs are now fixed.
  The first (the dataset is consistent — original = ground truth — but the
  port's result was not; e.g. index 3, a non-constant expression, was
  returned as the constant `-1`): the linear simplifier's term-partitioning
  helper took its remainder list **by value**, so terms it could not place
  into a disjoint partition were appended to a copy that the caller never saw
  and were silently dropped from the composed result. It now takes the list
  by reference, mirroring the Python oracle's list-by-reference semantics.
  The second: the refinement rule `x - (x&y) -> x&~y` (`checkBitwAndOpInSum`)
  was applied to products with more than two children (e.g. `(-1)*(b&d)*b`),
  silently dropping the extra factor and producing a non-equivalent result
  (e.g. `d-(b&d)*b` became `~b&d`); these bad rewrites are also what made
  17/100 expressions run to the per-expression deadline (they now all solve in
  well under it). The rule now requires the product to be exactly
  `(-1)*(conjunction)`.
  Regression guard: `MBA/diff_qsynth_ea.py` (ground-truth verification over
  the whole dataset).

# MSiMBA benchmark (semi-linear MBAs)

For each `data/MSiMBA/` file (the MSiMBA paper's semi-linear MBAs; 64-bit,
1000 expressions each) the implementations simplify the expressions and every
result is fast-checked against the dataset's `groundtruth` column. MSiMBA runs
on **all 1000** expressions (it is polynomial); SiMBA++ native and the GAMBA
port run on the **first 100** (the GAMBA general solver is exponential in the
variable count and is infeasible for the 5/6-var files, which are marked `—`;
reproduce with `python MBA/bench_msimba.py 100 64`, raw numbers:
`MBA/bench_msimba.csv`).

| test file | MSiMBA (s) | MSiMBA valid | SiMBA++ (s) | SiMBA++ valid | GAMBA (s) | GAMBA valid |
|---|---:|---:|---:|---:|---:|---:|
| e1_2vars | 0.6 | 1000/1000 | 0.2 | 8/100 | 0.1 | 91/100 |
| e1_3vars | 0.7 | 1000/1000 | 0.2 | 0/100 | 5.4 | 75/100 |
| e1_4vars | 1.3 | 1000/1000 | 0.2 | 0/100 | 63.7 | 94/100 |
| e1_5vars | 6.8 | 1000/1000 | 0.2 | 0/100 | — | — |
| e1_6vars | 38.1 | 1000/1000 | 0.2 | 0/100 | — | — |
| e2_2vars | 0.6 | 1000/1000 | 0.2 | 9/100 | 0.1 | 99/100 |
| e2_3vars | 0.7 | 1000/1000 | 0.2 | 0/100 | 13.3 | 87/100 |
| e2_4vars | 1.3 | 1000/1000 | 0.2 | 0/100 | 63.6 | 92/100 |
| e3_2vars | 0.6 | 1000/1000 | 0.2 | 9/100 | 0.1 | 81/100 |
| e3_3vars | 0.7 | 1000/1000 | 0.2 | 0/100 | 2.6 | 70/100 |
| e3_4vars | 1.4 | 1000/1000 | 0.2 | 0/100 | 70.7 | 98/100 |
| e4_2vars | 0.6 | 1000/1000 | 0.2 | 0/100 | 0.2 | 88/100 |
| e4_3vars | 0.7 | 1000/1000 | 0.2 | 0/100 | 2.6 | 73/100 |
| e4_4vars | 1.4 | 1000/1000 | 0.2 | 0/100 | 65.0 | 98/100 |
| e5_2vars | 0.6 | 1000/1000 | 0.2 | 5/100 | 0.1 | 73/100 |
| e5_3vars | 0.7 | 1000/1000 | 0.2 | 0/100 | 1.7 | 36/100 |
| e5_4vars | 1.7 | 1000/1000 | 0.2 | 0/100 | 69.4 | 92/100 |

`MSiMBA (s)`/`MSiMBA valid` are the native MSiMBA multi-bit simplifier over all
1000 expressions; `SiMBA++ (s)`/`SiMBA++ valid` are the native SiMBA++ **linear**
simplifier over the first 100; `GAMBA (s)`/`GAMBA valid` are the GAMBA native
port (general solver) over the first 100. The MSiMBA algorithm solves **every
expression in every file (17000/17000, 100%)** in under 40 s per file — the
semi-linear cases are exactly what it was designed for. The SiMBA++ native
linear simplifier cannot handle the semi-linear cases (constants inside bitwise
operands) and solves only a handful of the 2-var files that reduce to linear
form. The GAMBA general solver is correct but **exponential** in the variable
count: it is fast on 2-var files (~0.1 s) but takes 60–70 s on 4-var files and
is infeasible on 5/6-var files, and it does not reach 100% on any file.

# Comparison with CoBRA

Head-to-head against **CoBRA** (Trail of Bits' public MBA solver,
`github.com/trailofbits/CoBRA`) on CoBRA's own benchmark datasets plus
Saturn's simplification cases — 3,024 expressions, 64-bit — run as the
front-door `SiMBA++` (auto routing) vs `cobra-cli`.

**SiMBA++ is faster on every dataset — 4.3× faster overall (9.1 s vs 39.2 s) —
and both solvers are 100% correct (3018 solved + 6 already-minimal, 0 wrong,
0 real failures).**

| Dataset | N | SiMBA solved | CoBRA solved | SiMBA time | CoBRA time | speedup |
|---|---:|---:|---:|---:|---:|---:|
| univariate64 | 1000 | 1000 | 1000 | **1.7 s** | 2.5 s | 1.5× |
| multivariate64 | 1000 | 994 | 994 | **4.8 s** | 29.2 s | 6.1× |
| permutation64 | 13 | 13 | 13 | **0.43 s** | 4.7 s | 10.9× |
| obfuscatorx | 7 | 7 | 7 | **0.02 s** | 0.04 s | 2.0× |
| msimba | 1000 | 1000 | 1000 | **2.2 s** | 2.7 s | 1.3× |
| Saturn | 4 | 4 | 4 | **0.01 s** | 0.02 s | 2.0× |
| **Total** | **3024** | **3018** | **3018** | **9.1 s** | 39.2 s | **4.3×** |

Both solvers also correctly handle the 6 **already-minimal** cases in
multivariate64 — single-monomial `c*x0` inputs whose ground truth is identical
to the input (there is nothing to simplify). SiMBA++ leaves them unchanged; CoBRA
returns an equivalent reformat. The benchmark counts these as *trivial* (not
solves), so both score **3018 solved + 6 trivial = 3024/3024 correct**.

- **0 wrong results, 0 real failures** for both — every reported result is
  verified equivalent to the original (fast-check, with a solver-independent
  Python evaluator as a fallback verifier).
- **Solve-rate parity:** both simplify the same 3018 non-trivial expressions
  and correctly no-op the 6 already-minimal ones. The difference between the
  two solvers is purely speed.
- The largest **speed** wins are on the non-polynomial **multivariate64**
  (6.1×) and **permutation64** (10.9×) sets, where SiMBA++'s direct single-pass
  transform avoids CoBRA's iterative reconstruction.
- **obfuscatorx**: SiMBA++ simplifies each case in ~2 ms, *faster than CoBRA's
  ~5 ms* — a ~3000× speedup over SiMBA++'s own pre-optimization baseline
  (59.7 s for the set).
- On the 6 already-minimal cases CoBRA's "simplification" is a superficial
  reformat — e.g. re-encoding the coefficient in signed form and adding
  whitespace, `12468174789076196570*x0` → `-5978569284633355046 * x0` (the same
  value mod 2⁶⁴) — with no actual reduction, so it is not counted as a solve.

**Methodology.** `python comparison_bench.py` drives both front doors
(`SiMBA++ --mba=<e> --bitcount=64` and `cobra-cli --mba <e> --bitwidth 64`)
over `data/CoBRA/{univariate64,multivariate64,permutation64,obfuscatorx,msimba}.txt`
and Saturn's `data/saturn*` cases, flattening whitespace, and checks each
result for equivalence with the dataset ground truth (`mba_cli verify`,
falling back to the solver-independent Python evaluator). "solved" = a
non-empty result that differs from the input and verifies equivalent to the
ground truth; a case whose ground truth equals its input (already minimal) is
counted **trivial** (a correct no-op / reformat), not a solve.

# General Options

```
  --mba=<mba>                    - MBA that will be verified/simplified
  --mbadb=<mbadb>                - MBA database that will be verified/simplified
  --ir=<ir>                      - LLVM Module that contains MBA functions that will be verified/simplified
  --bitcount=<BitCount>          - Bitcount of the variables (Default: 64)
  --checklinear                  - Check if MBA is a linear expresssion (Default: true)
  --convert-to-llvm              - Converts the MBA database to LLVM
  --detect-simplify              - Search for MBAs in LLVM Module and try to simplify (Default: false)
  --fastcheck                    - Verify MBA with random values (Default: true)
  --prove                        - Prove with Z3 that the MBA is correct (Default: false)
  --simplify-expected            - Simplify the expected value to match it (Default: false)
  --ignore-expected              - Ignores the expected string (Default: false)
  --stop=<stop>                  - Stop after N MBAs are solved (Default: 0)
  --parallel                     - Evaluate/Check MBA expressions in parallel
  --optimize                     - Optimize LLVM IR before simplification (Default: true)
  --external-simplifier          - Use SiMBA/GAMBA or m as simplifier instead of internal (Path to simplify.py/simplify_general.py)
  --simplifier                 - MBA simplifier to use: native | general | external | msimba | auto (Default: auto)
  --auto-fallback                - In auto mode, try the other routes if the chosen one yields no result (Default: true)
  --max-var-count                - Max variable count for simplification (Default: 6)
  --min-ast-size                 - Minimum AST size for simplification (Default: 4)
  --walk-sub-ast                 - Walk sub AST if full AST does not match (Default: false)
  --print-smt                    - Print SMT2 formula for debugging purposes
  --timeout                      - Timeout in seconds for the Z3 solver / simplifiers (Default: 30)
  --accept-unknown               - Accept unknown as unsat (Accept long timeout as prove!) (Default: false)

```


# Performance

**All Tests are done on a MacBook Air M2 24GB**

![Alt text](images/performance.png "SiMBA++/SiMBA performance comparison")

# Comparison with HexRays Goomba

![Alt text](images/goomba.png "SiMBA++ easily outperforms Goomba")


# SiMBA++ on real world code as Clang/Clang++ plugin

![Alt text](images/openssl.png "SiMBA++ is able to find missed optimization opportunities by LLVM")
