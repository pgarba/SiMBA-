; ModuleID = 'mba_comb.c'
source_filename = "mba_comb.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

; Function Attrs: mustprogress nofree norecurse nosync nounwind readnone uwtable willreturn
define dso_local i64 @mba(i64 noundef %0, i64 noundef %1) local_unnamed_addr #0 {
  %3 = xor i64 %1, %0
  %4 = mul i64 %3, 3735936685
  %5 = or i64 %1, %0
  %6 = xor i64 %5, -1
  %7 = xor i64 %0, -1
  %8 = and i64 %7, %1
  %9 = add i64 %8, %6
  %10 = mul i64 %7, -4554857723717082365
  %11 = shl i64 %8, 1
  %12 = sub i64 %11, %3
  %13 = shl i64 %0, 1
  %14 = add i64 %12, %13
  %15 = and i64 %1, %0
  %16 = shl i64 %15, 1
  %17 = add i64 %16, %3
  %18 = shl i64 %6, 1
  %19 = add i64 %3, 2
  %20 = add i64 %19, %17
  %21 = add i64 %20, %14
  %22 = add i64 %21, %18
  %23 = mul i64 %14, -49374
  %24 = mul i64 %23, %22
  %25 = mul i64 %9, 4554857723717082365
  %26 = add i64 %10, 49374
  %27 = add i64 %26, %4
  %28 = add i64 %27, %17
  %29 = add i64 %28, %25
  %30 = add i64 %29, %24
  ret i64 %30
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind readnone uwtable willreturn
define dso_local i64 @mba_keep(i64 noundef %0, i64 noundef %1) local_unnamed_addr #0 {
  %3 = xor i64 %1, %0
  %4 = mul i64 %3, 4297347026588972322
  %5 = xor i64 %3, -1
  %6 = mul i64 %5, 4297347022853035637
  %7 = add i64 %1, %0
  %8 = mul i64 %7, -49374
  %9 = mul i64 %8, %7
  %10 = add i64 %7, 4297347022853085011
  %11 = add i64 %10, %4
  %12 = add i64 %11, %6
  %13 = add i64 %12, %9
  ret i64 %13
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind readnone uwtable willreturn "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 7, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{!"Ubuntu clang version 14.0.0-1ubuntu1"}
