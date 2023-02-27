; ModuleID = 'scramble4.c'
source_filename = "scramble4.c"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc19.34.31937"

$printf = comdat any

$__local_stdio_printf_options = comdat any

$"??_C@_0N@NBHKIKHE@Result?5?$CFllu?6?$AA@" = comdat any

@"??_C@_0N@NBHKIKHE@Result?5?$CFllu?6?$AA@" = linkonce_odr dso_local unnamed_addr constant [13 x i8] c"Result %llu\0A\00", comdat, align 1
@__local_stdio_printf_options._OptionsStorage = internal global i64 0, align 8

; Function Attrs: mustprogress nofree norecurse nosync nounwind readnone willreturn uwtable
define dso_local i64 @scramble(i64 noundef %0, i64 noundef %1) local_unnamed_addr #0 {
  %3 = add i64 %0, 1
  %4 = xor i64 %0, -1
  %5 = or i64 %1, %0
  %6 = add i64 %5, 1
  %7 = xor i64 %5, -1
  %8 = or i64 %7, %4
  %9 = add i64 %5, %3
  %10 = add i64 %9, %8
  %11 = and i64 %1, %0
  %12 = add i64 %11, %4
  %13 = or i64 %12, %5
  %14 = or i64 %12, %7
  %15 = mul i64 %11, -2
  %16 = add i64 %6, %3
  %17 = add i64 %16, %15
  %18 = add i64 %17, %14
  %19 = add i64 %18, %13
  %20 = and i64 %19, %7
  %21 = or i64 %20, %10
  %22 = add i64 %10, 1
  %23 = xor i64 %10, -1
  %24 = or i64 %20, %23
  %25 = add i64 %24, %22
  %26 = add i64 %25, %21
  %27 = or i64 %19, %7
  %28 = or i64 %26, %27
  %29 = or i64 %4, %1
  %30 = add i64 %29, %3
  %31 = add i64 %30, %5
  %32 = add i64 %31, %15
  %33 = or i64 %32, %5
  %34 = add i64 %33, 1
  %35 = xor i64 %33, -1
  %36 = and i64 %27, %33
  %37 = add i64 %36, %35
  %38 = or i64 %28, %37
  %39 = add i64 %28, 1
  %40 = xor i64 %28, -1
  %41 = or i64 %37, %40
  %42 = add i64 %34, %6
  %43 = add i64 %42, %39
  %44 = add i64 %43, %41
  %45 = add i64 %44, %38
  ret i64 %45
}

; Function Attrs: nounwind uwtable
define dso_local i32 @main() local_unnamed_addr #1 {
  %1 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull @"??_C@_0N@NBHKIKHE@Result?5?$CFllu?6?$AA@", i64 noundef 10746)
  ret i32 0
}

; Function Attrs: inlinehint nounwind uwtable
define linkonce_odr dso_local i32 @printf(ptr noundef %0, ...) local_unnamed_addr #2 comdat {
  %2 = alloca ptr, align 8
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %2) #7
  call void @llvm.va_start(ptr nonnull %2)
  %3 = load ptr, ptr %2, align 8, !tbaa !4
  %4 = call ptr @__acrt_iob_func(i32 noundef 1) #7
  %5 = call ptr @__local_stdio_printf_options()
  %6 = load i64, ptr %5, align 8, !tbaa !8
  %7 = call i32 @__stdio_common_vfprintf(i64 noundef %6, ptr noundef %4, ptr noundef %0, ptr noundef null, ptr noundef %3) #7
  call void @llvm.va_end(ptr nonnull %2)
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %2) #7
  ret i32 %7
}

; Function Attrs: argmemonly mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0(i64 immarg, ptr nocapture) #3

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.va_start(ptr) #4

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.va_end(ptr) #4

; Function Attrs: argmemonly mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0(i64 immarg, ptr nocapture) #3

; Function Attrs: noinline nounwind uwtable
define linkonce_odr dso_local ptr @__local_stdio_printf_options() local_unnamed_addr #5 comdat {
  ret ptr @__local_stdio_printf_options._OptionsStorage
}

declare dso_local ptr @__acrt_iob_func(i32 noundef) local_unnamed_addr #6

declare dso_local i32 @__stdio_common_vfprintf(i64 noundef, ptr noundef, ptr noundef, ptr noundef, ptr noundef) local_unnamed_addr #6

attributes #0 = { mustprogress nofree norecurse nosync nounwind readnone willreturn uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { nounwind uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { inlinehint nounwind uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { argmemonly mustprogress nocallback nofree nosync nounwind willreturn }
attributes #4 = { mustprogress nocallback nofree nosync nounwind willreturn }
attributes #5 = { noinline nounwind uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #6 = { "frame-pointer"="none" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #7 = { nounwind }

!llvm.module.flags = !{!0, !1, !2}
!llvm.ident = !{!3}

!0 = !{i32 1, !"wchar_size", i32 2}
!1 = !{i32 7, !"PIC Level", i32 2}
!2 = !{i32 7, !"uwtable", i32 2}
!3 = !{!"clang version 15.0.0"}
!4 = !{!5, !5, i64 0}
!5 = !{!"any pointer", !6, i64 0}
!6 = !{!"omnipotent char", !7, i64 0}
!7 = !{!"Simple C/C++ TBAA"}
!8 = !{!9, !9, i64 0}
!9 = !{!"long long", !6, i64 0}
