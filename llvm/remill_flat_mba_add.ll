; A remill flat-lifted x86-64 trace (SICE !LIFT SYM) of an MBA-obfuscated
; function: rax = (x ^ y) + 2*(x & y), i.e. x + y. x86 derives the flags of the
; final add from the obfuscated operands, so the sum %6 is both a result (an
; argument of __remill_flat_jump, register RAX) and an input of the OF/AF/PF
; trees. SiMBA++ must still simplify %6 itself to `add i64 %RDI, %RSI`.
; ModuleID = '/tmp/sice-lift-227860/lift.ll'
source_filename = "lifted_code"
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu-elf"

%union.vec128_t = type { %struct.uint128v1_t }
%struct.uint128v1_t = type { [1 x i128] }

define ptr @sub_401159(ptr %PC, ptr noalias %memory, ptr initializes((0, 8)) %NEXT_PC, i64 %RAX, i64 %RBX, i64 %RCX, i64 %RDX, i64 %RSI, i64 %RDI, ptr noalias initializes((-24, 0)) %RSP, ptr noalias %RBP, i64 %R8, i64 %R9, i64 %R10, i64 %R11, i64 %R12, i64 %R13, i64 %R14, i64 %R15, i64 %RIP, i64 %SS_BASE, i64 %GS_BASE, i64 %CS_BASE, i64 %FS_BASE, i8 %CF, i8 %PF, i8 %AF, i8 %ZF, i8 %SF, i8 %DF, i8 %OF, i64 %MM0, i64 %MM1, i64 %MM2, i64 %MM3, i64 %MM4, i64 %MM5, i64 %MM6, i64 %MM7, <2 x i64> %XMM0, <2 x i64> %XMM1, <2 x i64> %XMM2, <2 x i64> %XMM3, <2 x i64> %XMM4, <2 x i64> %XMM5, <2 x i64> %XMM6, <2 x i64> %XMM7, <2 x i64> %XMM8, <2 x i64> %XMM9, <2 x i64> %XMM10, <2 x i64> %XMM11, <2 x i64> %XMM12, <2 x i64> %XMM13, <2 x i64> %XMM14, <2 x i64> %XMM15, i128 %ST0, i128 %ST1, i128 %ST2, i128 %ST3, i128 %ST4, i128 %ST5, i128 %ST6, i128 %ST7) local_unnamed_addr {
  %pc_init = load i64, ptr %PC, align 8
  %1 = ptrtoint ptr %RBP to i64
  %rsp_slot248 = getelementptr i8, ptr %RSP, i64 -8
  store i64 %1, ptr %rsp_slot248, align 8
  %rsp_slot246 = getelementptr i8, ptr %RSP, i64 -16
  store i64 %RDI, ptr %rsp_slot246, align 8
  %rsp_slot251 = getelementptr i8, ptr %RSP, i64 -24
  store i64 %RSI, ptr %rsp_slot251, align 8
  %2 = add i64 %pc_init, 37
  store i64 %2, ptr %NEXT_PC, align 8
  %3 = xor i64 %RDI, %RSI
  %4 = and i64 %RDI, %RSI
  %5 = shl i64 %4, 1
  %6 = add i64 %5, %3
  %7 = xor i64 %6, %5
  %8 = lshr i64 %7, 63
  %9 = xor i64 %6, %3
  %10 = lshr i64 %9, 63
  %11 = add nuw nsw i64 %8, %10
  %12 = icmp eq i64 %11, 2
  %13 = zext i1 %12 to i8
  %14 = lshr i64 %6, 63
  %15 = trunc nuw nsw i64 %14 to i8
  %16 = icmp eq i64 %6, 0
  %17 = zext i1 %16 to i8
  %18 = xor i64 %7, %3
  %19 = trunc i64 %18 to i8
  %20 = lshr i8 %19, 4
  %21 = and i8 %20, 1
  %22 = trunc i64 %6 to i8
  %23 = tail call range(i8 0, 9) i8 @llvm.ctpop.i8(i8 %22)
  %24 = and i8 %23, 1
  %25 = xor i8 %24, 1
  %26 = icmp ult i64 %6, %5
  %27 = zext i1 %26 to i8
  store i64 %2, ptr %PC, align 8
  %rsp_i64 = ptrtoint ptr %rsp_slot248 to i64
  %28 = tail call ptr @__remill_flat_jump(ptr nonnull %PC, ptr %memory, ptr nonnull %NEXT_PC, i64 %6, i64 %RBX, i64 %RCX, i64 %3, i64 %RSI, i64 %RDI, i64 %rsp_i64, i64 %rsp_i64, i64 %R8, i64 %R9, i64 %R10, i64 %R11, i64 %R12, i64 %R13, i64 %R14, i64 %R15, i64 %RIP, i64 %SS_BASE, i64 %GS_BASE, i64 %CS_BASE, i64 %FS_BASE, i8 %27, i8 %25, i8 %21, i8 %17, i8 %15, i8 %DF, i8 %13, i64 %MM0, i64 %MM1, i64 %MM2, i64 %MM3, i64 %MM4, i64 %MM5, i64 %MM6, i64 %MM7, <2 x i64> %XMM0, <2 x i64> %XMM1, <2 x i64> %XMM2, <2 x i64> %XMM3, <2 x i64> %XMM4, <2 x i64> %XMM5, <2 x i64> %XMM6, <2 x i64> %XMM7, <2 x i64> %XMM8, <2 x i64> %XMM9, <2 x i64> %XMM10, <2 x i64> %XMM11, <2 x i64> %XMM12, <2 x i64> %XMM13, <2 x i64> %XMM14, <2 x i64> %XMM15, i128 %ST0, i128 %ST1, i128 %ST2, i128 %ST3, i128 %ST4, i128 %ST5, i128 %ST6, i128 %ST7)
  ret ptr %28
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i8 @llvm.ctpop.i8(i8) #0

; Function Attrs: mustprogress noinline nounwind optnone
declare dso_local ptr @__remill_flat_jump(ptr noundef, ptr noundef, ptr noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i8 noundef zeroext, i8 noundef zeroext, i8 noundef zeroext, i8 noundef zeroext, i8 noundef zeroext, i8 noundef zeroext, i8 noundef zeroext, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, ptr noundef byval(%union.vec128_t) align 8, ptr noundef byval(%union.vec128_t) align 8, ptr noundef byval(%union.vec128_t) align 8, ptr noundef byval(%union.vec128_t) align 8, ptr noundef byval(%union.vec128_t) align 8, ptr noundef byval(%union.vec128_t) align 8, ptr noundef byval(%union.vec128_t) align 8, ptr noundef byval(%union.vec128_t) align 8, ptr noundef byval(%union.vec128_t) align 8, ptr noundef byval(%union.vec128_t) align 8, ptr noundef byval(%union.vec128_t) align 8, ptr noundef byval(%union.vec128_t) align 8, ptr noundef byval(%union.vec128_t) align 8, ptr noundef byval(%union.vec128_t) align 8, ptr noundef byval(%union.vec128_t) align 8, ptr noundef byval(%union.vec128_t) align 8, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef, i64 noundef) local_unnamed_addr #1

attributes #0 = { mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #1 = { mustprogress noinline nounwind optnone "frame-pointer"="all" "min-legal-vector-width"="0" "no-builtins" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "tune-cpu"="generic" }
