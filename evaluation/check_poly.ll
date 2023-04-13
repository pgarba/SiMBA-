; ModuleID = 'check_poly.c'
source_filename = "check_poly.c"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128"
target triple = "arm64-apple-macosx13.0.0"

@str = private unnamed_addr constant [12 x i8] c"P(Q(X)) = X\00", align 1
@str.2 = private unnamed_addr constant [14 x i8] c"P(Q(X)) != X)\00", align 1

; Function Attrs: mustprogress nofree norecurse nosync nounwind readnone ssp willreturn uwtable
define zeroext i8 @P(i8 noundef zeroext %0) local_unnamed_addr #0 {
  %2 = mul i8 %0, -8
  %3 = add i8 %2, 97
  %4 = mul i8 %3, %0
  ret i8 %4
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind readnone ssp willreturn uwtable
define zeroext i8 @Q(i8 noundef zeroext %0) local_unnamed_addr #0 {
  %2 = mul i8 %0, -120
  %3 = add i8 %2, -95
  %4 = mul i8 %3, %0
  ret i8 %4
}

; Function Attrs: nofree nounwind ssp uwtable
define i32 @main(i32 noundef %0, ptr nocapture noundef readnone %1) local_unnamed_addr #1 {
  br label %3

3:                                                ; preds = %16, %2
  %4 = phi i8 [ 0, %2 ], [ %17, %16 ]
  %5 = zext i8 %4 to i32
  %6 = add nsw i32 %5, %0
  %7 = trunc i32 %6 to i8
  %8 = mul i8 %7, -120
  %9 = add i8 %8, -95
  %10 = mul i8 %9, %7
  %11 = mul i8 %10, -8
  %12 = add i8 %11, 97
  %13 = mul i8 %12, %10
  %14 = zext i8 %13 to i32
  %15 = icmp eq i32 %6, %14
  br i1 %15, label %16, label %19

16:                                               ; preds = %3
  %17 = add i8 %4, 1
  %18 = icmp eq i8 %17, 0
  br i1 %18, label %19, label %3, !llvm.loop !6

19:                                               ; preds = %16, %3
  %20 = phi ptr [ @str.2, %3 ], [ @str, %16 ]
  %21 = phi i32 [ -1, %3 ], [ 0, %16 ]
  %22 = tail call i32 @puts(ptr nonnull %20)
  ret i32 %21
}

; Function Attrs: nofree nounwind
declare noundef i32 @puts(ptr nocapture noundef readonly) local_unnamed_addr #2

attributes #0 = { mustprogress nofree norecurse nosync nounwind readnone ssp willreturn uwtable "frame-pointer"="non-leaf" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+crc,+crypto,+dotprod,+fp-armv8,+fp16fml,+fullfp16,+lse,+neon,+ras,+rcpc,+rdm,+sha2,+sha3,+sm4,+v8.5a,+zcm,+zcz" }
attributes #1 = { nofree nounwind ssp uwtable "frame-pointer"="non-leaf" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+crc,+crypto,+dotprod,+fp-armv8,+fp16fml,+fullfp16,+lse,+neon,+ras,+rcpc,+rdm,+sha2,+sha3,+sm4,+v8.5a,+zcm,+zcz" }
attributes #2 = { nofree nounwind }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 13, i32 1]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 7, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 1}
!5 = !{!"Homebrew clang version 15.0.7"}
!6 = distinct !{!6, !7}
!7 = !{!"llvm.loop.mustprogress"}
