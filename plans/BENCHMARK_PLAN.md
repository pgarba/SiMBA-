# Benchmark plan: vendored Python GAMBA vs. native C++ port

**Goal.** Compare the vendored Python GAMBA oracle (`external/GAMBA`) with the
native C++ port (`MBA/`) over **all** GAMBA test datasets, measuring for each
test file:

1. **Total time** to simplify all expressions (wall clock, summed per expression).
2. **Valid simplifications based on the ground truth** — the datasets ship
   `expr,groundtruth` lines; a result counts as *valid* when it is
   semantically equivalent to the ground truth (which is a correct
   simplification of the original, so `result ≡ groundtruth` ⟺
   `result ≡ original`).

## Method

- **Datasets** — all seven files under `external/GAMBA/experiments/datasets/`:
  `mba_obf_nonlinear` (1000 lines), `mba_flatten` (3000), `syntia` (500),
  `mba_obf_linear` (1000), `qsynth_ea` (500), `neureduce` (10000),
  `bonus/loki_tiny` (25000). Capped at the **first N=100 expressions** per file
  so the run stays bounded (the files span 500–25000 lines).
- **Bit width 8** — same convention as `solve_rate.py` / `diff_general.py`
  (8-bit keeps both implementations fast and the fast-check meaningful).
- **Per expression `expr,groundtruth`** (fresh process per call, both sides):
  - C++ port: `mba_cli.exe general 8 <expr>` — wall-timed; empty output = unsolved.
  - Python GAMBA: `python external/GAMBA/src/simplify_general.py -b 8` with the
    expression fed via **stdin** (interpreter `C:\Python\Python312\python.exe`,
    the one with numpy) — wall-timed; result parsed from the
    `*** ... simplified to <simpl>` marker; missing/empty = unsolved.
    (stdin is required because argparse treats an expression starting with `-`
    as an option when passed as a command-line argument.)
  - **Validity vs ground truth:** `mba_cli.exe verify 8 <result> <groundtruth>`
    — the 100-sample deterministic fast-check (`fastCheckEquivalent`, modular
    2^8 semantics). `EQUIVALENT` ⇒ valid.
- **Parallelism:** the seven files are benchmarked concurrently (one thread per
  file); expressions within a file run sequentially for clean timing.

## Metrics per file

| metric | definition |
|---|---|
| `time_cpp` / `time_py` | sum of per-expression wall time |
| `solved_cpp` / `solved_py` | non-empty result within the per-expression timeout |
| `valid_cpp` / `valid_py` | solved **and** fast-check equivalent to the ground truth |
| `speedup` | `time_py / time_cpp` |

Output: `MBA/bench_results.csv` + a console summary table; the numbers are
embedded in the README section *GAMBA benchmark (Python oracle vs. C++ port)*.

## Caveats

- Python time includes per-expression interpreter start-up (~1 s numpy import);
  the C++ binary start-up is ~ms. Methodology is otherwise identical
  (fresh process per expression, same inputs, same timeout caps: 30 s C++,
  60 s Python).
- Expressions using `>>`/`/`/`%` are not in GAMBA's grammar and are unsolved
  by both sides (counted as unsolved, not errors).
- "Valid" is a fast-check (100 random samples, no counterexample found), not an
  exhaustive or Z3 proof — a valid result is one the fast-check could not
  refute.

## Run

```
python MBA\bench_compare.py 100 8
```
