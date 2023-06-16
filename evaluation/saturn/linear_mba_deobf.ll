; ModuleID = 'linear_mba.ll'
source_filename = "lifted_code"
target datalayout = "e-m:o-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-apple-macosx-macho"

; Function Attrs: mustprogress nofree norecurse nosync nounwind null_pointer_is_valid readnone ssp willreturn uwtable
define dllexport i64 @F_100003ac4(i64 noundef %Arg_0, i64 noundef %Arg_1, i64 noundef %Arg_2) local_unnamed_addr #0 {
entry:
  %0 = add i64 %Arg_1, %Arg_0
  %1 = shl i64 %0, 2
  %2 = shl i64 %Arg_2, 1
  %3 = add i64 %1, %2
  ret i64 %3
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind null_pointer_is_valid readnone ssp willreturn uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+cx16,+cx8,+fxsr,+mmx,+sahf,+sse,+sse2,+sse3,+sse4.1,+ssse3,+x87" "tune-cpu"="generic" }
