; ModuleID = 'xor_op_pred.c'
source_filename = "xor_op_pred.c"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc19.34.31937"

$printf = comdat any

$__local_stdio_printf_options = comdat any

$"??_C@_07DDAIHLFP@z?5?$DN?5?$CFd?6?$AA@" = comdat any

$"?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA" = comdat any

@"??_C@_07DDAIHLFP@z?5?$DN?5?$CFd?6?$AA@" = linkonce_odr dso_local unnamed_addr constant [8 x i8] c"z = %d\0A\00", comdat, align 1
@"?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA" = linkonce_odr dso_local global i64 0, comdat, align 8

; Function Attrs: mustprogress norecurse uwtable
define dso_local noundef i32 @main(i32 noundef %0, ptr nocapture noundef readonly %1) local_unnamed_addr #0 {
  %3 = getelementptr inbounds ptr, ptr %1, i64 1
  %4 = load ptr, ptr %3, align 8, !tbaa !5
  %5 = tail call i32 @atoi(ptr nocapture noundef %4)
  %6 = getelementptr inbounds ptr, ptr %1, i64 2
  %7 = load ptr, ptr %6, align 8, !tbaa !5
  %8 = tail call i32 @atoi(ptr nocapture noundef %7)
  %9 = and i32 %5, 255
  %10 = and i32 %8, 255
  %11 = xor i32 %10, %9
  %12 = xor i32 %9, -1
  %13 = and i32 %10, %12
  %14 = shl nuw nsw i32 %13, 1
  %15 = add i32 %8, %11
  %16 = sub i32 %5, %15
  %17 = add i32 %16, %14
  %18 = and i32 %17, 255
  %19 = icmp eq i32 %18, 23
  br i1 %19, label %20, label %22

20:                                               ; preds = %2
  %21 = or i32 %10, %9
  br label %48

22:                                               ; preds = %2
  %23 = mul nuw nsw i32 %9, 97
  %24 = xor i32 %5, -1
  %25 = and i32 %10, %24
  %26 = mul nuw nsw i32 %25, 194
  %27 = shl nuw nsw i32 %25, 1
  %28 = add nuw nsw i32 %11, %10
  %29 = mul nuw nsw i32 %28, 255
  %30 = add nuw nsw i32 %9, 163
  %31 = add nuw nsw i32 %30, %27
  %32 = add nuw nsw i32 %31, %29
  %33 = mul nuw nsw i32 %9, 248
  %34 = mul nuw nsw i32 %25, 240
  %35 = shl nuw nsw i32 %28, 3
  %36 = add nuw nsw i32 %33, 232
  %37 = add nuw nsw i32 %36, %34
  %38 = add nuw nsw i32 %37, %35
  %39 = mul nsw i32 %32, %38
  %40 = mul i32 %15, 159
  %41 = add nuw nsw i32 %23, 138
  %42 = add nuw nsw i32 %41, %26
  %43 = add i32 %42, %40
  %44 = add i32 %43, %39
  %45 = and i32 %44, 252
  %46 = icmp ult i32 %45, 100
  %47 = select i1 %46, i32 %11, i32 0
  br label %48

48:                                               ; preds = %22, %20
  %49 = phi i32 [ %21, %20 ], [ %47, %22 ]
  %50 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull @"??_C@_07DDAIHLFP@z?5?$DN?5?$CFd?6?$AA@", i32 noundef %49)
  ret i32 0
}

; Function Attrs: argmemonly mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0(i64 immarg, ptr nocapture) #1

; Function Attrs: mustprogress nofree nounwind readonly willreturn
declare dso_local i32 @atoi(ptr nocapture noundef) local_unnamed_addr #2

; Function Attrs: inlinehint mustprogress uwtable
define linkonce_odr dso_local i32 @printf(ptr noundef %0, ...) local_unnamed_addr #3 comdat {
  %2 = alloca ptr, align 8
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %2) #7
  call void @llvm.va_start(ptr nonnull %2)
  %3 = load ptr, ptr %2, align 8, !tbaa !5
  %4 = call ptr @__acrt_iob_func(i32 noundef 1)
  %5 = call ptr @__local_stdio_printf_options()
  %6 = load i64, ptr %5, align 8, !tbaa !9
  %7 = call i32 @__stdio_common_vfprintf(i64 noundef %6, ptr noundef %4, ptr noundef %0, ptr noundef null, ptr noundef %3)
  call void @llvm.va_end(ptr nonnull %2)
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %2) #7
  ret i32 %7
}

; Function Attrs: argmemonly mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0(i64 immarg, ptr nocapture) #1

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.va_start(ptr) #4

declare dso_local ptr @__acrt_iob_func(i32 noundef) local_unnamed_addr #5

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.va_end(ptr) #4

declare dso_local i32 @__stdio_common_vfprintf(i64 noundef, ptr noundef, ptr noundef, ptr noundef, ptr noundef) local_unnamed_addr #5

; Function Attrs: mustprogress noinline nounwind uwtable
define linkonce_odr dso_local ptr @__local_stdio_printf_options() local_unnamed_addr #6 comdat {
  ret ptr @"?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA"
}

attributes #0 = { mustprogress norecurse uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { argmemonly mustprogress nocallback nofree nosync nounwind willreturn }
attributes #2 = { mustprogress nofree nounwind readonly willreturn "frame-pointer"="none" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { inlinehint mustprogress uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { mustprogress nocallback nofree nosync nounwind willreturn }
attributes #5 = { "frame-pointer"="none" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #6 = { mustprogress noinline nounwind uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #7 = { nounwind }

!llvm.linker.options = !{!0}
!llvm.module.flags = !{!1, !2, !3}
!llvm.ident = !{!4}

!0 = !{!"/FAILIFMISMATCH:\22_CRT_STDIO_ISO_WIDE_SPECIFIERS=0\22"}
!1 = !{i32 1, !"wchar_size", i32 2}
!2 = !{i32 7, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{!"clang version 15.0.0"}
!5 = !{!6, !6, i64 0}
!6 = !{!"any pointer", !7, i64 0}
!7 = !{!"omnipotent char", !8, i64 0}
!8 = !{!"Simple C++ TBAA"}
!9 = !{!10, !10, i64 0}
!10 = !{!"long long", !7, i64 0}
