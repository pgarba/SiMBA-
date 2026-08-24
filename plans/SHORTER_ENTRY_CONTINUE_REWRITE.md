# Shorter Entry/Continue Rewrite — lifted.ll `@check`

> **Read this first.** The verify() width fix is DONE (see `plans/REMAINING_WORK_VERIFY_FIX.md`).
> Today the entry/continue checks are **preserved** as the original 5-op roundabout (correct).
> This plan pursues a **correct, shorter** rewrite of those two checks. It is OPTIONAL: if it does
> not verify cleanly, we REVERT and keep the 5-op roundabout. Do not force it.

---

## 1. Key finding (corrects the old plan's section D)

The old plan (`REMAINING_WORK_VERIFY_FIX.md` §D) claimed the entry check was a **bit-7 test**
(`and i8 %8, -128; icmp eq 0`, true for 128 values). **That is WRONG.**

Empirically verified (all 256 byte values; see §8 decode script): the roundabout is true **iff the
byte is 0**. It is a **null-byte / C-string-terminator test**, i.e. **`byte == 0`**.

- Roundabout (`zext/shl 56/ashr 32/and mask/icmp eq 0`)  ≡  `byte == 0`  (true only for byte 0).
- Old plan's `and i8 %8, -128; icmp eq 0`  ≡  `byte < 128`  (true for 128 values)  → **NOT equivalent**.
- **Correct shorter form: `icmp eq i8 %byte, 0`** (1 op vs 5 ops).

## 2. The roundabout structure (what to match)

Entry check = `llvm/lifted.ll` L10–14; continue check = L43–47 (identical shape, different regs):

```
%a = zext i8 %byte to i64
%b = shl  nuw i64 %a, 56
%c = ashr exact i64 %b, 32
%d = and  i64 %c, 72057594021150720      ; = 0x00FFFFFFFF000000
%e = icmp eq i64 %d, 0                    ;  ← root (i1), the MBA
```

Mask `72057594021150720 = 0x00FFFFFFFF000000` (bits 24..55). The whole thing reduces to `byte == 0`.

## 3. Why the existing candidate framework CANNOT produce it

- The AST **root is an `icmp` (i1) ⇒ `BitWidth = 1`**.
- `eval()` (`ShuttingYard.cpp:849`) and `createLLVMReplacement()` (`ShuttingYard.cpp:410`)
  evaluate/materialize candidates **at `BitWidth=1`**.
- A 1-bit candidate can only express functions of **bit 0** of the byte: `a`, `~a`, `!a`, `0`, `-1`
  (the set built in `tryCandidateSimplifications`, `LLVMParser.cpp:1404`).
- `byte == 0` depends on **all 8 bits** ⇒ **no such candidate can represent it**.
- The verify() width fix (samples the byte at full 8-bit width) **correctly rejects every one** of
  them as value-wrong. That is why the checks are preserved today.

⇒ The shorter rewrite must be a **direct IR rewrite** that recognizes the roundabout and emits
`icmp eq i8 %byte, 0`, **bypassing** the `BitWidth=1` string-candidate flow.

## 4. Approach (targeted IR rewrite)

1. **Detect** the roundabout: a 5-instruction chain `zext i8→i64` → `shl 56` → `ashr 32` →
   `and 0x00FFFFFFFF000000` → `icmp eq …,0` whose `zext` source is a **narrow (i8) value**.
2. **Emit** `icmp eq i8 %byte, 0` at the insertion point.
3. **Replace** the roundabout root's (`%e`) uses with the new icmp; **erase** the now-unused
   roundabout instructions (repeat to fixpoint, mirroring the existing DCE at `LLVMParser.cpp:510-530`).
4. Guard it so it only fires on the exact structure (narrow-byte source, the exact constants), so it
   cannot mis-fire on other expressions.

**Where to add:** `LLVMParser.cpp`. Best as a small helper invoked from the apply-replacements
region (~line 480–530) or as a separate detection+rewrite pass over the function's instructions.
Reference points: `extractCandidates`, `findReplacements`, `verify` (line 828),
`tryCandidateSimplifications` (line 1404), the DCE block (line 510).

## 5. Correctness argument

- Roundabout ≡ `byte == 0` (verified for all 256 values, §8).
- `icmp eq i8 %byte, 0` ≡ `byte == 0`.
- ⇒ The rewrite is **value-equivalent** and strictly shorter (5 ops → 1 op).

## 6. Verification / acceptance — REQUIRED before keeping the rewrite

Run **all** of these; if any fails, REVERT (§7).

1. **Rebuild:** `build\do_rebuild.bat` → confirm `rebuild.log` shows `NMAKE_EXIT=0`.
2. **Clean run:** `.\build\SiMBA++.exe --ir llvm/lifted.ll --detect-simplify --out build\lifted_short.ll`
   → confirm `EXIT=0`.
3. **Inspect `build\lifted_short.ll`:**
   - Entry & continue checks are now `icmp eq i8 %x, 0` (1 op each).
   - Accumulator still 4 ops (`sext i8→i64, add -48, mul 10, add`).
   - Loop-exit check unchanged (the 4-op `byte==0` via `and -256 / zext / or / icmp`).
   - **No `trunc i8 %x to i1`** anywhere; `opt -passes=verify` still valid.
4. **opt verify:** `g:\saturn_windows\deps\tob_libraries\llvm\bin\opt.exe -passes=verify build\lifted_short.ll` → `EXIT=0`.
5. **C differential harness (gold standard):** recreate `build\chk_harness.c` + `chk_run.bat`
   (they were deleted). Compile ORIGINAL `llvm/lifted.ll` and SIMPLIFIED `build\lifted_short.ll`
   with `clang-cl` (from the LLVM bin in §9), run both on the same inputs, confirm
   **ORIGINAL == SIMPLIFIED** for every input (including bytes 0..255 and multi-byte strings).
6. **GAMBA suite:** `python MBA\run_all_tests.py` → **all 9 PASS** (the change is in `LLVMParser.cpp`,
   separate from the `MBA/` native port, but confirm it stays green). Do NOT modify `external/GAMBA/` or `MBA/`.

## 7. Rollback (if it does NOT verify cleanly)

- Revert the edit (e.g. `git checkout -- LLVMParser.cpp`, or undo the change).
- Rebuild; re-run the clean run → the entry/continue checks return to the 5-op roundabout.
- **Do not ship** a rewrite that fails any step in §6. The 5-op roundabout is correct and acceptable.

## 8. Decode check (re-run to re-confirm the `byte == 0` finding)

Save as `build\_decode_check.py`, then `python build\_decode_check.py`:

```python
import ctypes
def i64(x): return ctypes.c_int64(x & 0xFFFFFFFFFFFFFFFF).value
MASK = 0x00FFFFFFFF000000  # 72057594021150720
def ashr64(v, n):
    vu = v & 0xFFFFFFFFFFFFFFFF; sign = (vu >> 63) & 1
    s = vu >> n
    if sign: s |= (0xFFFFFFFFFFFFFFFF << (64 - n)) & 0xFFFFFFFFFFFFFFFF
    return ctypes.c_int64(s).value
def roundabout(b):
    p9 = i64(b); p10 = i64((p9 << 56) & 0xFFFFFFFFFFFFFFFF)
    p11 = ashr64(p10, 32); p12 = i64((p11 & MASK) & 0xFFFFFFFFFFFFFFFF)
    return p12 == 0
true_round = [b for b in range(256) if roundabout(b)]
print("roundabout TRUE for bytes:", true_round)
print("roundabout == (b==0)?", true_round == [0])
print("roundabout == (b<128)?", true_round == list(range(128)))
```

Expected: `roundabout TRUE for bytes: [0]`, `roundabout == (b==0)? True`, `roundabout == (b<128)? False`.

## 9. Key locations

- Workspace: `C:\github\SiMBA-`
- Build: `build\do_rebuild.bat` (NMake/MSVC x64) → `build\SiMBA++.exe`; log `build\rebuild.log` (check `NMAKE_EXIT=0`).
  MSVC env: `call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64`
- Run: `.\build\SiMBA++.exe --ir llvm/lifted.ll --detect-simplify [--simba-debug] --out <out.ll>`
- LLVM toolchain (use THIS, not `C:\Program Files\LLVM\bin`):
  `g:\saturn_windows\deps\tob_libraries\llvm\bin` (clang/clang-cl/llc/lli/opt/llvm-as).
- Target IR: `llvm\lifted.ll` (84 lines; entry check L10–15, continue check L43–48,
  accumulator L21–32, loop-exit L33–37).
- verify() fix: `LLVMParser.cpp` ~line 828–892.
- Candidate flow: `LLVMParser.cpp` `tryCandidateSimplifications` ~1404–1454; apply loop ~480–530.
- eval / createLLVMReplacement: `ShuttingYard.cpp` (eval :849, createLLVMReplacement :410).
- Deliverable from the prior session: `build\lifted_final.ll` (checks preserved as 5-op roundabout).
- Plans: `plans\REMAINING_WORK_VERIFY_FIX.md` (completed), this file.

## 10. Done-checklist (tick as you go)

- [ ] Re-ran §8 decode check → roundabout ≡ `byte == 0` confirmed
- [ ] Targeted pattern added; rebuild `NMAKE_EXIT=0`
- [ ] Clean run `EXIT=0`; entry/continue now `icmp eq i8 %x, 0`; no `trunc i8 %x to i1`
- [ ] `opt -passes=verify` passes
- [ ] C differential harness ORIGINAL == SIMPLIFIED
- [ ] GAMBA suite all 9 PASS
- [ ] (if any step failed) reverted; checks left as the 5-op roundabout
