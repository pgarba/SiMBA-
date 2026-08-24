# Z3 Proving Speedup Plan

## Problem

Z3 proving (`--prove`) is expensive. The current pipeline
(`simplify & bit-blast & smt` in `Z3Prover.cpp:prove()`) is **exponential in
the bit width** for expressions with variable multiplication, timing out at the
30 s deadline on 32/64-bit inputs.

## Root Cause (measured)

A standalone benchmark (`/tmp/z3_bench.cpp`, Z3 5.0.0) comparing solver
strategies on a multiplication-heavy QF_BV conjecture
(`(x+y)*(x-y) + z*z  ==  x*x - y*y + z*z`):

| bit width | current `simplify&bit-blast&smt` | `QF_BV` logic | `smt` only |
|---:|---:|---:|---:|
| 8    | 907 ms (UNSAT)  | 0.07 ms (UNSAT) | 0.07 ms (UNSAT) |
| 32   | **3002 ms (TIMEOUT)** | 0.08 ms (UNSAT) | 0.08 ms (UNSAT) |
| 64   | **5000 ms (TIMEOUT)** | 0.08 ms (UNSAT) | 0.08 ms (UNSAT) |

The `bit-blast` tactic converts bit-vector operations to boolean circuits, then
solves with a SAT solver. For multiplication (a complex bit-vector op), the
bit-blasted circuit is huge and the SAT solver struggles — exponential in the
bit width. The `QF_BV` logic (and plain `smt`) use **word-level reasoning**,
which handles multiplication in polynomial time.

**The `bit-blast` tactic is the culprit.** Removing it gives a 10,000–60,000x
speedup on multiplication-heavy expressions, turning timeouts into sub-millisecond
solves.

## Why the current pipeline uses `bit-blast`

The `simplify & bit-blast & smt` pipeline is a generic "make it work" approach:
`simplify` does basic rewriting, `bit-blast` reduces everything to booleans, and
`smt` solves the resulting SAT problem. It works for simple expressions but is
the wrong tool for word-level bit-vector reasoning.

## Plan

### Phase 1: Replace the tactic with `QF_BV` logic (most impactful)

**Change** `Z3Prover.cpp:prove()`:

```cpp
// Before:
auto t = (z3::tactic(c, "simplify") & z3::tactic(c, "bit-blast") &
          z3::tactic(c, "smt"));
Solver = new z3::solver(t.mk_solver());

// After:
Solver = new z3::solver(c, "QF_BV");
```

The `QF_BV` logic tells Z3 the problem is quantifier-free bit-vectors, enabling
word-level reasoning (no bit-blasting). This is the single most impactful change.

**Expected impact:** 10,000–60,000x speedup on multiplication-heavy expressions;
timeouts become sub-millisecond solves. Simple expressions are already fast and
stay fast.

**Risk:** Low. `QF_BV` is the standard logic for this problem class. The
conjecture is always quantifier-free (no `forall`/`exists`), so `QF_BV` is
always applicable.

**Verification:** Re-run the benchmark; confirm all expressions solve UNSAT
(equivalent) or SAT (not equivalent) correctly, with no timeouts.

### Phase 2: Reuse the Z3 context (moderate impact)

Currently `proveReplacement()` (Z3Prover.cpp:100) creates a **new `z3::context`
per call**. Context creation is expensive (allocates the Z3 kernel, sorts, etc.).

**Change:** Use a single global context (like the existing `Z3CtxGlobal` in
LLVMParser.cpp) instead of creating one per call. The `prove()` function already
has a global `Solver` pointer with context-checking logic; extend this to also
cache the context.

**Expected impact:** Moderate speedup (saves context creation overhead per call).
Matters most when proving many expressions in a batch.

**Risk:** Low. The existing `resetZ3Solver`/`abandonZ3Solver` machinery already
handles context lifecycle; extend it to the context itself.

### Phase 3: Set solver parameters (minor impact)

Set a few QF_BV-specific parameters to optimize performance:

```cpp
Solver->set("timeout", static_cast<unsigned>(timeout * 1000));
// Optional: disable features we don't need
Solver->set("model", false);        // we only need sat/unsat, not a model
Solver->set("proof", false);        // no proof production
```

**Expected impact:** Minor speedup (a few %). The `model=false` and `proof=false`
params skip work Z3 does by default that we don't use.

**Risk:** Very low. These params only disable optional features.

### Phase 4: Reduce the default timeout (minor impact)

The default timeout is 30 s. With the `QF_BV` logic, most expressions solve in
sub-millisecond time. A 30 s timeout means a genuinely hard expression will burn
30 s before failing.

**Change:** Reduce the default timeout to 5 s (or make it adaptive based on
expression size).

**Expected impact:** Faster failure on genuinely hard expressions (5 s instead of
30 s). No impact on easy expressions (they solve in ms).

**Risk:** Low. Expressions that need >5 s are likely infeasible anyway; the
fast-check gate remains the sound fallback.

## What NOT to do

### Implementing our own prover

For small expressions (few variables, small bit width), a custom prover (e.g.,
BDD-based or a custom word-level solver) could be faster than Z3. But:

- Z3 with `QF_BV` is already sub-millisecond on most expressions.
- A custom prover is a large engineering effort (correctness is hard).
- The fast-check gate already provides a sound fallback for the cases Z3 can't
  prove.

**Recommendation:** Don't implement a custom prover. The `QF_BV` logic fix is
the 95% solution with 5% of the effort.

### Using a different SMT solver

Alternatives (CVC5, MathSAT, Yices2) exist, but:

- Z3 is already vendored and linked; switching is a large change.
- Z3 with `QF_BV` is competitive with the others on QF_BV.
- The fast-check gate remains the sound fallback regardless.

**Recommendation:** Stick with Z3; fix the tactic.

## Status (implemented)

- **Phase 1 — DONE:** `Z3Prover.cpp:prove()` now uses `z3::solver(c, "QF_BV")`
  instead of `simplify & bit-blast & smt`. Turns timeouts into sub-millisecond
  solves on multiplication-heavy expressions.
- **Phase 2 — DONE:** `proveReplacement()` now uses a cached global context
  (`getProverCtx()`) instead of creating a fresh `z3::context` per call.
- **Phase 3 — DONE:** `model=false` and `proof=false` set on the solver.
- **Phase 4 — DEFERRED:** The `--timeout` flag is shared between the Z3 solver
  and the MBA simplifiers (external + general deadlines in
  `SimplifierRouter.cpp`). Reducing it globally would hurt the simplifiers. A
  separate Z3-only timeout would be a larger change; deferred until needed.

**Verification:** All 6 LLVM tests pass (`tests/run_tests_llvm_simplify.py`).
Z3 proving measured at sub-millisecond per expression (5.5 ms/expr total
including ~5 ms process startup, over 20 linear 64-bit expressions).

## Reproducing the benchmark

```bash
Z3DIR=/home/adam/saturn_21_1/saturn/deps/tob_libraries/z3
g++ -O2 -std=c++17 /tmp/z3_bench.cpp -I$Z3DIR/include -L$Z3DIR/lib -lz3 -o /tmp/z3_bench
LD_LIBRARY_PATH=$Z3DIR/lib /tmp/z3_bench 64 5000 3
```
