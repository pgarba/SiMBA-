; ModuleID = 'check_mba_lifted.ll'
source_filename = "lifted_code"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128"
target triple = "arm64-apple-macosx13.0.0"

; Function Attrs: mustprogress nofree norecurse nosync nounwind null_pointer_is_valid readnone ssp willreturn uwtable
define dllexport i64 @F_100003ea0(i64 noundef %Arg_0, i64 noundef %Arg_1) local_unnamed_addr #0 {
entry:
  ret i64 0
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind null_pointer_is_valid readnone ssp willreturn uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="penryn" "target-features"="+cx16,+cx8,+fxsr,+mmx,+sahf,+sse,+sse2,+sse3,+sse4.1,+ssse3,+x87" "tune-cpu"="generic" }
