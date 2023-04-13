; ModuleID = 'linear_mba.c'
source_filename = "linear_mba.c"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128"
target triple = "arm64-apple-macosx13.0.0"

@.str = private unnamed_addr constant [17 x i8] c"%s(%d, %d) = %d\0A\00", align 1
@.str.1 = private unnamed_addr constant [3 x i8] c"E1\00", align 1
@.str.2 = private unnamed_addr constant [3 x i8] c"E2\00", align 1
@.str.3 = private unnamed_addr constant [3 x i8] c"E3\00", align 1

; Function Attrs: mustprogress nofree norecurse nosync nounwind readnone ssp willreturn uwtable
define zeroext i8 @E1(i8 noundef zeroext %0, i8 noundef zeroext %1) local_unnamed_addr #0 {
  %3 = add i8 %1, %0
  ret i8 %3
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind readnone ssp willreturn uwtable
define zeroext i8 @E2(i8 noundef zeroext %0, i8 noundef zeroext %1) local_unnamed_addr #0 {
  %3 = xor i8 %1, %0
  %4 = and i8 %1, %0
  %5 = shl i8 %4, 1
  %6 = add i8 %5, %3
  ret i8 %6
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind readnone ssp willreturn uwtable
define zeroext i8 @E3(i8 noundef zeroext %0, i8 noundef zeroext %1) local_unnamed_addr #0 {
  %3 = xor i8 %1, %0
  %4 = and i8 %1, %0
  %5 = shl i8 %4, 1
  %6 = add i8 %5, %3
  ret i8 %6
}

; Function Attrs: nofree nounwind ssp uwtable
define i32 @main(i32 noundef %0, ptr nocapture noundef readonly %1) local_unnamed_addr #1 {
  %3 = getelementptr inbounds ptr, ptr %1, i64 1
  %4 = load ptr, ptr %3, align 8, !tbaa !6
  %5 = tail call i32 @atoi(ptr nocapture noundef %4)
  %6 = getelementptr inbounds ptr, ptr %1, i64 2
  %7 = load ptr, ptr %6, align 8, !tbaa !6
  %8 = tail call i32 @atoi(ptr nocapture noundef %7)
  %9 = and i32 %5, 255
  %10 = and i32 %8, 255
  %11 = add i32 %8, %5
  %12 = and i32 %11, 255
  %13 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull @.str, ptr noundef nonnull @.str.1, i32 noundef %9, i32 noundef %10, i32 noundef %12)
  %14 = xor i32 %8, %5
  %15 = and i32 %8, %5
  %16 = shl i32 %15, 1
  %17 = add i32 %16, %14
  %18 = and i32 %17, 255
  %19 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull @.str, ptr noundef nonnull @.str.2, i32 noundef %9, i32 noundef %10, i32 noundef %18)
  %20 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull @.str, ptr noundef nonnull @.str.3, i32 noundef %9, i32 noundef %10, i32 noundef %18)
  ret i32 0
}

; Function Attrs: mustprogress nofree nounwind readonly willreturn
declare i32 @atoi(ptr nocapture noundef) local_unnamed_addr #2

; Function Attrs: nofree nounwind
declare noundef i32 @printf(ptr nocapture noundef readonly, ...) local_unnamed_addr #3

attributes #0 = { mustprogress nofree norecurse nosync nounwind readnone ssp willreturn uwtable "frame-pointer"="non-leaf" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+crc,+crypto,+dotprod,+fp-armv8,+fp16fml,+fullfp16,+lse,+neon,+ras,+rcpc,+rdm,+sha2,+sha3,+sm4,+v8.5a,+zcm,+zcz" }
attributes #1 = { nofree nounwind ssp uwtable "frame-pointer"="non-leaf" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+crc,+crypto,+dotprod,+fp-armv8,+fp16fml,+fullfp16,+lse,+neon,+ras,+rcpc,+rdm,+sha2,+sha3,+sm4,+v8.5a,+zcm,+zcz" }
attributes #2 = { mustprogress nofree nounwind readonly willreturn "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+crc,+crypto,+dotprod,+fp-armv8,+fp16fml,+fullfp16,+lse,+neon,+ras,+rcpc,+rdm,+sha2,+sha3,+sm4,+v8.5a,+zcm,+zcz" }
attributes #3 = { nofree nounwind "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+crc,+crypto,+dotprod,+fp-armv8,+fp16fml,+fullfp16,+lse,+neon,+ras,+rcpc,+rdm,+sha2,+sha3,+sm4,+v8.5a,+zcm,+zcz" }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 13, i32 1]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 7, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 1}
!5 = !{!"Homebrew clang version 15.0.7"}
!6 = !{!7, !7, i64 0}
!7 = !{!"any pointer", !8, i64 0}
!8 = !{!"omnipotent char", !9, i64 0}
!9 = !{!"Simple C/C++ TBAA"}
