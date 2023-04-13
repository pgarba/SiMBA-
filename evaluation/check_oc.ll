; ModuleID = 'check_oc.c'
source_filename = "check_oc.c"
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128"
target triple = "arm64-apple-macosx13.0.0"

@str = private unnamed_addr constant [15 x i8] c"OC(x, y) = 123\00", align 1
@str.2 = private unnamed_addr constant [17 x i8] c"OC(x, y) != 123)\00", align 1

; Function Attrs: mustprogress nofree norecurse nosync nounwind readnone ssp willreturn uwtable
define zeroext i8 @OC(i8 noundef zeroext %0, i8 noundef zeroext %1) local_unnamed_addr #0 {
  %3 = zext i8 %0 to i32
  %4 = mul nuw nsw i32 %3, 97
  %5 = zext i8 %1 to i32
  %6 = xor i32 %3, -1
  %7 = and i32 %5, %6
  %8 = mul nuw nsw i32 %7, 194
  %9 = xor i32 %5, %3
  %10 = shl nuw nsw i32 %7, 1
  %11 = add nuw nsw i32 %9, %5
  %12 = mul nuw nsw i32 %11, 255
  %13 = add nuw nsw i32 %3, 163
  %14 = add nuw nsw i32 %13, %10
  %15 = add nuw nsw i32 %14, %12
  %16 = mul nuw nsw i32 %3, 248
  %17 = mul nuw nsw i32 %7, 240
  %18 = shl nuw nsw i32 %11, 3
  %19 = add nuw nsw i32 %16, 232
  %20 = add nuw nsw i32 %19, %17
  %21 = add nuw nsw i32 %20, %18
  %22 = mul nsw i32 %15, %21
  %23 = mul nuw nsw i32 %11, 159
  %24 = add nuw nsw i32 %4, 195
  %25 = add nuw nsw i32 %24, %8
  %26 = add nuw nsw i32 %25, %23
  %27 = add nuw i32 %26, %22
  %28 = trunc i32 %27 to i8
  ret i8 %28
}

; Function Attrs: nofree nounwind ssp uwtable
define i32 @main(i32 noundef %0, ptr nocapture noundef readnone %1) local_unnamed_addr #1 {
  br label %3

3:                                                ; preds = %37, %2
  %4 = phi i8 [ 0, %2 ], [ %38, %37 ]
  %5 = zext i8 %4 to i32
  %6 = mul nuw nsw i32 %5, 97
  %7 = xor i32 %5, -1
  %8 = add nuw nsw i32 %5, 163
  %9 = mul nuw nsw i32 %5, 248
  %10 = add nuw nsw i32 %9, 232
  %11 = add nuw nsw i32 %6, 195
  br label %12

12:                                               ; preds = %34, %3
  %13 = phi i8 [ 0, %3 ], [ %35, %34 ]
  %14 = zext i8 %13 to i32
  %15 = and i32 %14, %7
  %16 = mul nuw nsw i32 %15, 194
  %17 = xor i32 %14, %5
  %18 = shl nuw nsw i32 %15, 1
  %19 = add nuw nsw i32 %17, %14
  %20 = mul nuw nsw i32 %19, 255
  %21 = add nuw nsw i32 %8, %18
  %22 = add nuw nsw i32 %21, %20
  %23 = mul nuw nsw i32 %15, 240
  %24 = shl nuw nsw i32 %19, 3
  %25 = add nuw nsw i32 %10, %23
  %26 = add nuw nsw i32 %25, %24
  %27 = mul nsw i32 %22, %26
  %28 = mul nuw nsw i32 %19, 159
  %29 = add nuw nsw i32 %11, %16
  %30 = add nuw nsw i32 %29, %28
  %31 = add nuw i32 %30, %27
  %32 = and i32 %31, 255
  %33 = icmp eq i32 %32, 123
  br i1 %33, label %34, label %40

34:                                               ; preds = %12
  %35 = add i8 %13, 1
  %36 = icmp eq i8 %35, 0
  br i1 %36, label %37, label %12, !llvm.loop !6

37:                                               ; preds = %34
  %38 = add i8 %4, 1
  %39 = icmp eq i8 %38, 0
  br i1 %39, label %40, label %3, !llvm.loop !8

40:                                               ; preds = %37, %12
  %41 = phi ptr [ @str.2, %12 ], [ @str, %37 ]
  %42 = phi i32 [ -1, %12 ], [ 0, %37 ]
  %43 = tail call i32 @puts(ptr nonnull %41)
  ret i32 %42
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
