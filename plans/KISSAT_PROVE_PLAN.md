# Kissat SAT Proving for Semi-Linear MBAs — Plan

## Goal

Provide a **complete, sound, independent** proof for the semi-linear
conjectures that word-level solvers time out on:

```
sum_i c_i * f_i(vars)  ==  target          (e.g. x + y)
```

"Independent" = does not trust the MSiMBA normalizer (unlike the
canonical-form / `--accept-unknown` fast path). The proof artifact is
"Kissat says UNSAT on the bit-blasted circuit."

## Why SAT (measured, not assumed)

The word-level route is a dead end for this shape — measured on the hard
conjecture (`/tmp/conj.smt2` 64-bit, `/tmp/conj32.smt2` 32-bit):

| solver | 32-bit | 64-bit |
|---|---|---|
| Z3 QF_BV | unknown @ 30 s | unknown @ 60 s |
| **Bitwuzla QF_BV** (already vendored+built) | **unknown @ 60 s** | **unknown @ 60 s** |
| Z3 bit-blast + Z3 internal SAT | unknown @ 30 s | — |

So: bit-blast to a boolean circuit and hand it to a **dedicated SAT solver**
(Kissat), which is built for exactly these hardware-verification-scale
instances and is far faster than Z3's internal SAT.

## Approach

```
parse expr0, expr1  ──>  z3::expr  (reuse getZ3ExprFromString)
        │
        ▼
  conj = (expr0 ^ expr1) OR-reduced to 1 bit   [pure boolean]
        │
        ▼
  Z3 "bit-blast" tactic  ──>  boolean circuit (and/or/not over bool vars)
        │                      (32-bit: ~81k exprs, ~2 MB)
        ▼
  Tseitin transform (OUR CODE)  ──>  DIMACS CNF
        │                              + map: CNF var -> input-bit
        ▼
  Kissat  ──>  UNSAT ⇒ proved | SAT ⇒ counterexample | UNKNOWN ⇒ unproved
```

Z3 is used **only as a parser + bit-blaster** (fast, no solving). All solving
is Kissat.

## Key integration findings (already de-risked)

1. **Z3 `bit-blast` works** and yields a *pure boolean circuit* (top=`or`,
   81,543 exprs at 32-bit). Confirmed with `/tmp/z3_dimacs2.cpp`.
2. **Z3 `goal::dimacs()` does NOT work** on the bit-blasted goal — it returns
   null because the goal has let-bindings and is not in CNF. **Do not use it.**
3. **Fix: Tseitin ourselves.** Walk the bit-blasted boolean `z3::expr` tree
   (only `and`/`or`/`not`/`xor`/`implies` over boolean leaves), introduce a
   fresh CNF variable per gate, emit the defining clauses, and replace. This
   is ~100 lines and also gives the **CNF-var → input-bit map** needed to turn
   a SAT model into a concrete counterexample.
4. **Conjecture must be a pure boolean before bit-blasting.** `(distinct A B)`
   over bit-vectors is left unexpanded by `bit-blast`. Rewrite as
   `OR_i (extract_i(A ^ B)) == #b1` (OR-reduction of the XOR) — then
   bit-blasting gives a clean boolean circuit.
5. **Kissat is fetchable**: `https://github.com/ArminBiere/kissat` (single
   `kissat.h` + `kissat.c`, MIT). Internet access confirmed.
6. **Fallback SAT solver already built**: CaDiCaL at
   `external/bitwuzla/build/subprojects/cadical-rel-2.1.2/src/libcadical.a`
   (same author as Kissat, equally capable). Use if vendoring Kissat is
   blocked.

## Steps

### Step 0 — Benchmark (½ day, GATE for the rest)

Build a standalone benchmark (`/tmp/kissat_bench`):
1. Clone Kissat → `external/kissat/` (pin commit).
2. Reuse `/tmp/z3_dimacs2.cpp` to bit-blast `/tmp/conj32.smt2` +
   `/tmp/conj.smt2`; add the Tseitin encoder; emit CNF.
3. Run Kissat on each CNF.

**Measure:** CNF size (vars/clauses), solve time, result.
**Success criteria (to proceed):**
- 32-bit conjecture → **UNSAT in < 1 s**.
- 64-bit conjecture → **UNSAT in < 30 s** (whole formula).

**If whole-formula 64-bit is too big/slow:** fall back to **per-bit**
instances — bit-blast each output bit `extract_i(expr0) != extract_i(expr1)`
separately (32 or 64 smaller CNFs, trivially parallel). Each is ~1/width the
size. Re-measure; per-bit should be comfortably fast.

**Stop condition:** if Kissat can't solve even the 32-bit instance in < 1 s,
the approach is wrong — stop and reconsider (better multiplier encoding, or a
custom bit-blaster). Do not proceed to integration.

### Step 1 — Vendor Kissat + CMake (½ day)

- `external/kissat/` — `kissat.h` + `kissat.c` (MIT, pinned commit + a
  `VERSION` note).
- `CMakeLists.txt`: build static lib `kissat`, link into `SiMBA++` (mirror the
  existing Z3 link pattern).

### Step 2 — SAT prover module (1–2 days)

New `MBA/KissatProver.{h,cpp}`:

```cpp
// Returns: 1 = proved (UNSAT), 0 = not proved (SAT or UNKNOWN),
// and fills `counterexample` (var -> value) when SAT.
int proveEquivalentSAT(const std::string &orig, const std::string &simp,
                       int bitCount, long timeoutMs,
                       std::map<std::string,uint64_t> *counterexample);
```

Pipeline:
1. Parse both sides to `z3::expr` — **refactor** `getZ3ExprFromString`
   (Z3Prover.cpp) into a shared helper so the SAT path and QF_BV path use the
   same parser.
2. Build the pure-boolean conjecture (OR-reduced XOR, finding #4).
3. `z3::goal` + `bit-blast` tactic → boolean circuit.
4. **Tseitin encoder** (finding #3) → CNF + input-bit map.
5. Feed CNF to Kissat via `kissat_read_line` (no temp file).
6. `kissat_set_resource_limit` as the timeout proxy — **calibrate**
   nodes-per-second in Step 0 so `timeoutMs` maps to a node limit.
7. Result: UNSAT ⇒ 1; SAT ⇒ read model over the input-bit vars, reconstruct
   each variable's value, fill `counterexample`, return 0; UNKNOWN ⇒ 0.

### Step 3 — Wire into the prove path (½ day)

In `proveReplacement` (Z3Prover.cpp) / `proveEquivalent` (MBA/Verify.cpp):
- Try **QF_BV first** (sub-millisecond on the easy cases — keep it).
- On UNKNOWN/timeout, **fall back to `proveEquivalentSAT`**.
- The msimba path already calls `proveEquivalent`, so it benefits
  automatically. Add a `--no-sat-fallback` flag to disable (for the honest
  Z3-only metric the user chose).

### Step 4 — Validate + report (½ day)

- Cross-check: run `tests/run_prove_tests.py` with the SAT fallback. It **must
  agree with Z3 on every case Z3 solved** and prove a **superset** (the 4+-var
  cases that currently time out).
- Report three columns: `proved (Z3)`, `proved (Kissat fallback)`,
  `unproved`.
- Optional (later): Kissat **DRAT proof logs** + an external checker for
  machine-checkable proof artifacts.

## Soundness argument

Each link is independently trustworthy:
- **Parse** — existing, tested (GAMBA parse / shunting yard).
- **Bit-blast** — Z3's internal, sound, standard tactic.
- **Tseitin** — our code, but a textbook transformation; unit-test it on small
  formulas against a brute-force truth table.
- **Kissat** — competition-grade, well-tested; UNSAT is a sound certificate.

The chain never invokes the MSiMBA normalizer, so the proof is independent of
it.

## Risks / mitigations

| risk | mitigation |
|---|---|
| 64-bit whole-formula CNF too big for Kissat | per-bit instances (parallel); re-measure in Step 0 |
| Z3 `bit-blast` circuit uses gates Tseitin doesn't handle | restrict to and/or/not/xor/implies; assert + fallback on unknown gate |
| resource-limit ↔ time calibration off | calibrate in Step 0; expose raw node limit too |
| counterexample reconstruction bugs | only needed on SAT (rare); validate against fast-check |
| Kissat vendor blocked | CaDiCaL already built (finding #6) |

## Effort

~**3–4 days** total. Step 0 (½ day) is the gate — it produces the real
CNF-size and solve-time numbers that justify the rest.

## Files

- `external/kissat/` — vendored Kissat (new).
- `MBA/KissatProver.{h,cpp}` — SAT prover + Tseitin encoder (new).
- `Z3Prover.cpp` — refactor shared parse helper; QF_BV→SAT fallback hook.
- `MBA/Verify.cpp` — `proveEquivalent` calls the fallback.
- `CMakeLists.txt` — link Kissat.
- `tests/run_prove_tests.py` — add `proved (Kissat)` column.
- Scratch benchmarks: `/tmp/z3_dimacs2.cpp`, `/tmp/kissat_bench`,
  `/tmp/conj32.cnf`, `/tmp/conj64.cnf`.
