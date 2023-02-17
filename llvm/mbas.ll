; ModuleID = 'mbas.c'
source_filename = "mbas.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

; Function Attrs: mustprogress nofree norecurse nosync nounwind readnone uwtable willreturn
define dso_local i64 @mba0(i64 noundef %0, i64 noundef %1) local_unnamed_addr #0 {
  %3 = xor i64 %1, %0
  %4 = and i64 %1, %0
  %5 = shl nsw i64 %4, 1
  %6 = add nsw i64 %5, %3
  ret i64 %6
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind readnone uwtable willreturn
define dso_local i64 @mba1(i64 noundef %0, i64 noundef %1, i64 noundef %2) local_unnamed_addr #0 {
  %4 = xor i64 %2, -1
  %5 = xor i64 %1, -1
  %6 = and i64 %5, %2
  %7 = add i64 %6, %1
  %8 = xor i64 %7, -1
  %9 = and i64 %8, %2
  %10 = add i64 %7, %9
  %11 = and i64 %10, %4
  %12 = xor i64 %1, %0
  %13 = and i64 %1, %0
  %14 = shl i64 %13, 1
  %15 = add i64 %14, %12
  %16 = and i64 %15, %0
  %17 = shl i64 %16, 1
  %18 = xor i64 %0, -1
  %19 = or i64 %18, %2
  %20 = add i64 %19, %0
  %21 = sub i64 -2, %20
  %22 = and i64 %21, %2
  %23 = xor i64 %15, %0
  %24 = and i64 %7, %1
  %25 = or i64 %10, %7
  %26 = add i64 %20, 1
  %27 = add i64 %26, %2
  %28 = add i64 %27, %22
  %29 = add i64 %28, %23
  %30 = add i64 %29, %24
  %31 = add i64 %30, %17
  %32 = add i64 %31, %11
  %33 = sub i64 %32, %25
  %34 = shl i64 %33, 1
  ret i64 %34
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind readnone uwtable willreturn
define dso_local i32 @mba2(i32 noundef %0, i32 noundef %1, i32 noundef %2) local_unnamed_addr #0 {
  %4 = xor i32 %1, -1
  %5 = or i32 %4, %0
  %6 = or i32 %5, %2
  %7 = add i32 %0, 1
  %8 = xor i32 %0, -1
  %9 = xor i32 %2, -1
  %10 = or i32 %9, %8
  %11 = mul i32 %10, -4
  %12 = xor i32 %9, %0
  %13 = xor i32 %9, %1
  %14 = and i32 %13, %0
  %15 = mul i32 %14, -7
  %16 = and i32 %4, %2
  %17 = xor i32 %16, %0
  %18 = and i32 %16, %0
  %19 = and i32 %9, %4
  %20 = or i32 %19, %8
  %21 = mul i32 %20, 5
  %22 = and i32 %1, %0
  %23 = xor i32 %22, %2
  %24 = add i32 %18, %23
  %25 = sub i32 %7, %24
  %26 = shl i32 %25, 1
  %27 = add i32 %17, %6
  %28 = mul i32 %27, 3
  %29 = add i32 %12, -2
  %30 = add i32 %29, %11
  %31 = add i32 %30, %15
  %32 = add i32 %31, %21
  %33 = add i32 %32, %26
  %34 = add i32 %33, %28
  ret i32 %34
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind readnone uwtable willreturn "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 7, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 1}
!4 = !{!"Ubuntu clang version 14.0.0-1ubuntu1"}
