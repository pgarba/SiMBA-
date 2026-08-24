<!--
  PROMPT FOR THE NEXT SESSION — copy everything between the <PROMPT> markers and paste it.
  It is self-contained: it points at the plan, states the key finding, and gives exact commands.
-->

<PROMPT>

Pursue a correct, SHORTER rewrite of the entry/continue checks in `llvm/lifted.ll` (`@check`).
FIRST read `plans/SHORTER_ENTRY_CONTINUE_REWRITE.md` in full — it is the source of truth for the
approach, the exact commands, file locations, and the done-checklist. Do not re-derive the
background; pick up from it.

Context in one line: the verify() width fix is already DONE. Today the entry/continue checks are
preserved as the original 5-op roundabout (`zext i8→i64 / shl 56 / ashr 32 / and 0x00FFFFFFFF000000 /
icmp eq 0`), which is correct. We want to shorten them to the correct 1-op form.

KEY FINDING (already verified — re-confirm with the §8 decode script in the plan): that roundabout
computes **`byte == 0`** (a null-byte / C-string-terminator test), NOT a bit-7 test. So the correct
shorter form is **`icmp eq i8 %byte, 0`** (1 op). The old plan's `and i8 %8, -128; icmp eq 0`
suggestion was WRONG (it is `byte < 128`, true for 128 values) — do NOT use it.

WHY it needs a targeted IR rewrite (not the existing candidate flow): the AST root is an `icmp` (i1)
so `BitWidth=1`, and `eval()`/`createLLVMReplacement()` (ShuttingYard.cpp) evaluate candidates at
`BitWidth=1`, which can only express functions of bit 0 of the byte. `byte == 0` needs all 8 bits, so
no string candidate can represent it (the verify() fix correctly rejects them all). So add a small
**direct IR rewrite** in `LLVMParser.cpp` that detects the exact roundabout structure (narrow i8
source + the exact constants) and emits `icmp eq i8 %byte, 0`, replacing the roundabout root's uses
and erasing the now-unused instructions (mirror the existing DCE at LLVMParser.cpp:510).

Your job:
1. Re-run the plan's §8 decode check to re-confirm the roundabout ≡ `byte == 0`.
2. Implement the targeted IR rewrite in `LLVMParser.cpp` (see plan §4 for where). Rebuild with
   `build\do_rebuild.bat` → confirm `rebuild.log` shows `NMAKE_EXIT=0`.
3. Clean run: `.\build\SiMBA++.exe --ir llvm/lifted.ll --detect-simplify --out build\lifted_short.ll`
   → confirm `EXIT=0`, and inspect the output: entry & continue checks now `icmp eq i8 %x, 0` (1 op);
   accumulator still 4 ops; loop-exit unchanged; **no `trunc i8 %x to i1`** anywhere.
4. `g:\saturn_windows\deps\tob_libraries\llvm\bin\opt.exe -passes=verify build\lifted_short.ll` → `EXIT=0`.
5. Recreate the C differential harness (it was deleted): compile ORIGINAL `llvm/lifted.ll` and
   SIMPLIFIED `build\lifted_short.ll` with `clang-cl` from the LLVM bin above, run both on the same
   inputs (bytes 0..255 and multi-byte strings), confirm **ORIGINAL == SIMPLIFIED** for every input.
6. Re-run the GAMBA suite: `python MBA\run_all_tests.py` → confirm **all 9 PASS**.
   Do NOT modify `external/GAMBA/` or `MBA/`.

ROLLBACK — REQUIRED if anything fails: if ANY step above fails (build, EXIT≠0, wrong output,
opt-verify fails, differential mismatch, or a GAMBA test breaks), REVERT the `LLVMParser.cpp` change
(e.g. `git checkout -- LLVMParser.cpp`), rebuild, and leave the entry/continue checks as the original
5-op roundabout. Do NOT ship a rewrite that does not verify cleanly. The 5-op roundabout is correct
and acceptable — a shorter form is a nice-to-have, not a requirement.

Acceptance / done when: either (a) the shorter `icmp eq i8 %x, 0` rewrite is in place AND passes all of
steps 3–6 (opt-verify, differential ORIGINAL==SIMPLIFIED, all 9 GAMBA PASS), or (b) it was reverted and
the checks remain the 5-op roundabout. Tick off the checklist at the bottom of the plan as you go and
report each result.

</PROMPT>
