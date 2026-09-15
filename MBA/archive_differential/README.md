# Archived GAMBA differential tests (2026-09-15)

These scripts were the C++-port vs vendored-Python-oracle differential tests.
Commit `3d44203` removed the vendored oracle (`external/GAMBA/src/`) once the
port was complete, so every script here either crashes at import
(`ModuleNotFoundError`) or loads 0 expressions / hits a hardcoded Windows
path. They served their purpose during the port and are kept here (and in git
history) for reference only — they are **not** run by `run_all_tests.py`.

They were additionally counted as PASS by the old runner, which ignored child
exit codes (see plans/REMAINING_WORK_PLAN.md, P0). The runner now fails on
non-zero exits.

Current verification weight:
- `MBA/test_tier2_semantics.py` (native semantics, run by `run_all_tests.py`)
- `tests/run_prove_tests.py` (honest Z3 prove metric)
- `tests/test_canonical.py` (ground-truth canonical validation)
