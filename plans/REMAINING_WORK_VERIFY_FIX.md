# Remaining Work — verify() width fix (lifted.ll `@check`)

> **Read this first in a fresh session.** The verify() false-accept bug is FIXED and verified
> for `llvm/lifted.ll`. A few confirmations + cleanup remain before this is fully done.

---

## 1. What was done (complete — do not redo)

**Bug.** `LLVMParser::verify()` sampled each opaque variable at `BitWidth` bits, where
`BitWidth` is the AST **root** width. For a candidate whose root is an `icmp` (i1),
`BitWidth == 1`, so the random quick-test only fed the variable values `0/1`. A value-wrong
candidate (`~a`, i.e. `trunc i8 %x to i1`) happened to agree with the original on that 1-bit
slice and was wrongly accepted. Result: the entry/continue checks in `@check` were rewritten to
`trunc i8 %x to i1` (bit 0), which is **value-wrong** for full 8-bit bytes.

**Fix (in `LLVMParser.cpp`, `verify()`, ~line 872–892).** Each opaque variable is now assigned
its **full actual type width** (`Variables[j]->getType()->getIntegerBitWidth()`, 64 for
pointers) instead of `BitWidth`. So a byte (i8) is sampled across all 256 values, and the
`~a` candidate now fails the quick test (mismatch) and is rejected.

```cpp
int VarWidth = 64;
if (!Variables[j]->getType()->isPointerTy()) {
  int W = Variables[j]->getType()->getIntegerBitWidth();
  if (W > 0 && W <= 64)
    VarWidth = W;
}
uint64_t Truncated =
    (VarWidth >= 64) ? v : (v & ((uint64_t(1) << VarWidth) - 1));
par.push_back(APInt(VarWidth, Truncated, false));
```

All `[VERIFY-DBG]` / `[AST-DBG]` debug prints added during diagnosis were **removed**.

**Verified so far (this session):**
- Build: `build\do_rebuild.bat` → `NMAKE_EXIT=0`.
- `SiMBA++.exe --ir llvm/lifted.ll --detect-simplify --simba-debug --out build\lifted_dbg23.ll`
  → `EXIT=0`. Log shows `[*] [VERIFY] mismatch '~a' R0=0 R1=-1` (rejected, twice).
- Only two simplifications applied: exit `46048+18446744073709550382*b` (2 ops) and
  accumulator `((sext[8:64](b)+-48)+(a*10))` (4 ops). Entry/continue checks are **preserved**
  as the original 5-op roundabout (zext/shl/ashr/and/icmp) — no `trunc i8 %x to i1`.
- C differential harness (`build\chk_run.bat`): ORIGINAL == SIMPLIFIED for r1..r4
  (`r1=-8347704956746735136 r2=-1036101143415496224 r3=7611366417671752370
  r4=2991897110088507872`).
- `opt -passes=verify build\lifted_dbg23.ll` → `EXIT=0`.

---

## 2. What is STILL needed

### A. Re-run the GAMBA differential suite (key confirmation) — REQUIRED
The verify() change is in `LLVMParser.cpp` (separate from the `MBA/` native port), so it
should not affect the suite — but **confirm** it stays all-PASS.

```
cd C:\github\SiMBA-
python MBA\run_all_tests.py
```
Expect **all 9 tests PASS** (`parse`, `parse_dataset`, `node`, `refine`,
`phase4_expand_factorize`, `subst`, `bitwise`, `simplify_linear`, `general`).
Do **NOT** modify `external/GAMBA/` or `MBA/`.

### B. Final clean run of the tool (no --simba-debug) — REQUIRED
Confirm normal output is correct and the tool exits 0:
```
.\build\SiMBA++.exe --ir llvm/lifted.ll --detect-simplify --out build\lifted_final.ll
```
Then confirm `build\lifted_final.ll` matches the verified output (entry/continue checks
preserved, accumulator 4 ops, no `trunc i8 %x to i1`) and passes
`opt -passes=verify`. Note the first log line
`[*] [VERIFY] mismatch '18446744073709551568+10*a+b'` is a **pre-existing, non-fatal**
baseline (MBAPATTERN takes over; tool still exits 0).

### C. Cleanup of disposable files — REQUIRED
Delete (all under `C:\github\SiMBA-\build\`):
`lifted_dbg*.ll`, `run*.log`, `chk_harness.c`, `chk_run.bat`, `chk_run.log`,
`chk_orig.exe`, `chk_simpl.exe`, `chk_harness.o`, `orig.o`, `simpl.o`,
`verify_final.log`, `gamba_run.txt`, `gamba_suite.txt`, any stray `*.o`/`*.bc`/`*.s`.
Keep `build\SiMBA++.exe`, `build\do_rebuild.bat`, `build\rebuild.log`.

### D. (Optional / nice-to-have) A correct shorter rewrite for the entry/continue checks
The current outcome (checks preserved as the original 5-op roundabout) is **acceptable and
correct**. If desired, a genuinely shorter *correct* rewrite would be even better, e.g. the
real bit-7-clear test: `and i8 %8, -128` then `icmp eq 0` (byte in [0,127]). This requires
the simplifier to actually produce that form and verify() to accept it — do **not** force it;
only pursue if it verifies cleanly. The loop-exit check (5-op `byte==0`) is already correct;
leave it.

> **CORRECTION (later session):** the "bit-7-clear test" description above is **WRONG**. The
> roundabout actually computes **`byte == 0`** (a null-byte / C-string-terminator test), verified
> for all 256 values — NOT `byte < 128`. So `and i8 %8, -128; icmp eq 0` is **not** equivalent.
> The correct shorter form is **`icmp eq i8 %byte, 0`** (1 op). Because the AST root is an `icmp`
> (i1, `BitWidth=1`), the existing string-candidate flow cannot express it; a **targeted IR rewrite**
> is required. See the dedicated plan **`plans/SHORTER_ENTRY_CONTINUE_REWRITE.md`** (and its prompt
> **`plans/PROMPT_SHORTER_REWRITE.md`**) for the approach, exact commands, verification, and rollback.

---

## 3. Key locations (for the fresh session)

- Workspace: `C:\github\SiMBA-`
- Build: `build\do_rebuild.bat` (NMake/MSVC x64) → `build\SiMBA++.exe`; log `build\rebuild.log`
  (check `NMAKE_EXIT=0`).
- Run: `.\build\SiMBA++.exe --ir llvm/lifted.ll --detect-simplify [--simba-debug] --out <out.ll>`
- LLVM toolchain (use this, NOT `C:\Program Files\LLVM\bin`):
  `g:\saturn_windows\deps\tob_libraries\llvm\bin` (clang/clang-cl/llc/lli/opt/llvm-as).
- MSVC env: `call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64`
- The fix: `LLVMParser.cpp`, `verify()`, ~line 872–892.
- Target IR: `llvm\lifted.ll` (84 lines; entry check L10–15, continue check L36–41,
  accumulator L21–32, loop-exit L33–37).
- C harness: `build\chk_harness.c` + `build\chk_run.bat` (disposable).
- GAMBA suite: `MBA\run_all_tests.py` (9 tests, all must PASS).
- Plans: `plans\GAMBA_INTEGRATION_PLAN.md` (suite status), this file.

---

## 4. Done-checklist (tick as you go)
- [x] `python MBA\run_all_tests.py` → all 9 PASS
- [x] Clean `--detect-simplify` run → `EXIT=0`, output correct, `opt -passes=verify` passes
- [x] Disposable build artifacts deleted
- [x] (optional) correct shorter entry/continue rewrite, only if it verifies cleanly — **left as-is**: simplifier does not emit the shorter `and i8 %8, -128; icmp eq 0` form (it tried `a`/`~a`/`0`/`-1`, all rejected by verify(), so the original 5-op roundabout is preserved). Per plan guidance ("do not force it"), the preserved 5-op form is correct and acceptable.
