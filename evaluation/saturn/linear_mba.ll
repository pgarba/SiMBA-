; ModuleID = 'lifted_code'
source_filename = "lifted_code"
target datalayout = "e-m:o-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-apple-macosx-macho"

; Function Attrs: mustprogress nofree norecurse nosync nounwind null_pointer_is_valid readnone ssp uwtable willreturn
define dllexport i64 @F_100003ac4(i64 noundef %Arg_0, i64 noundef %Arg_1, i64 noundef %Arg_2) local_unnamed_addr #0 {
entry:
  %0 = xor i64 %Arg_1, -1
  %1 = and i64 %0, %Arg_2
  %2 = add i64 %1, %Arg_1
  %3 = xor i64 %2, -1
  %4 = and i64 %3, %Arg_2
  %5 = add i64 %4, %2
  %6 = xor i64 %Arg_2, -1
  %7 = and i64 %5, %6
  %8 = xor i64 %Arg_1, %Arg_0
  %9 = and i64 %Arg_1, %Arg_0
  %10 = shl i64 %9, 1
  %11 = add i64 %10, %8
  %12 = and i64 %11, %Arg_0
  %13 = shl i64 %12, 1
  %14 = xor i64 %Arg_0, -1
  %15 = or i64 %14, %Arg_2
  %16 = add i64 %15, %Arg_0
  %17 = sub i64 -2, %16
  %18 = and i64 %17, %Arg_2
  %19 = xor i64 %11, %Arg_0
  %20 = and i64 %2, %Arg_1
  %21 = add i64 %Arg_2, 1
  %22 = add i64 %21, %16
  %23 = add i64 %22, %20
  %24 = or i64 %5, %2
  %25 = add i64 %23, %19
  %26 = add i64 %25, %18
  %27 = add i64 %26, %13
  %28 = add i64 %27, %7
  %29 = sub i64 %28, %24
  %30 = shl i64 %29, 1
  ret i64 %30
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind null_pointer_is_valid readnone ssp uwtable willreturn "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+cx16,+cx8,+fxsr,+mmx,+sahf,+sse,+sse2,+sse3,+sse4.1,+ssse3,+x87" "tune-cpu"="generic" }
