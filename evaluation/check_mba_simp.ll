; ModuleID = 'check_mba.ll'
source_filename = "check_mba.c"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128"
target triple = "arm64-apple-macosx13.0.0"

@str = private unnamed_addr constant [12 x i8] c"E(x, y) = 0\00", align 1
@str.2 = private unnamed_addr constant [14 x i8] c"E(x, y) != 0)\00", align 1

; Function Attrs: mustprogress nofree norecurse nosync nounwind readnone ssp willreturn uwtable
define zeroext i8 @E(i8 noundef zeroext %0, i8 noundef zeroext %1) local_unnamed_addr #0 {
  ret i8 0
}

; Function Attrs: nofree nounwind ssp uwtable
define i32 @main(i32 noundef %0, ptr nocapture noundef readnone %1) local_unnamed_addr #1 {
  br label %3

3:                                                ; preds = %17, %2
  %4 = phi i8 [ 0, %2 ], [ %18, %17 ]
  %5 = xor i8 %4, -1
  br label %6

6:                                                ; preds = %14, %3
  %7 = phi i8 [ 0, %3 ], [ %15, %14 ]
  %8 = and i8 %7, %5
  %9 = mul i8 %8, -2
  %10 = xor i8 %7, %4
  %11 = add i8 %7, %10
  %12 = sub i8 %4, %11
  %13 = icmp eq i8 %12, %9
  br i1 %13, label %14, label %.loopexit

14:                                               ; preds = %6
  %15 = add i8 %7, 1
  %16 = icmp eq i8 %15, 0
  br i1 %16, label %17, label %6, !llvm.loop !6

17:                                               ; preds = %14
  %18 = add i8 %4, 1
  %19 = icmp eq i8 %18, 0
  br i1 %19, label %.loopexit, label %3, !llvm.loop !8

.loopexit:                                        ; preds = %17, %6
  %20 = phi ptr [ @str.2, %6 ], [ @str, %17 ]
  %21 = phi i32 [ -1, %6 ], [ 0, %17 ]
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
!8 = distinct !{!8, !7}
