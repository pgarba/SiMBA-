; ModuleID = 'lifted_code'
source_filename = "lifted_code"
target datalayout = "e-m:o-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-apple-macosx-macho"

@RAM = external global [0 x i8]

; Function Attrs: mustprogress nofree norecurse nosync nounwind null_pointer_is_valid readnone ssp uwtable willreturn
define dllexport i64 @F_100003ea0(i64 noundef %Arg_0, i64 noundef %Arg_1) local_unnamed_addr #0 {
entry:
  %0 = and i64 %Arg_1, 127
  %1 = and i64 %Arg_0, 127
  %2 = xor i64 %1, -1
  %3 = and i64 %0, %2
  %4 = xor i64 %Arg_1, %Arg_0
  %5 = shl nuw nsw i64 %3, 1
  %6 = add i64 %4, %Arg_1
  %7 = sub i64 %Arg_0, %6
  %8 = add i64 %7, %5
  %9 = and i64 %8, 255
  ret i64 %9
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind null_pointer_is_valid readnone ssp uwtable willreturn "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+cx16,+cx8,+fxsr,+mmx,+sahf,+sse,+sse2,+sse3,+sse4.1,+ssse3,+x87" "tune-cpu"="generic" }
