; ModuleID = 'c:\github\SiMBA-\llvm\vm_obf.ll'
source_filename = "vm_obf.c"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc19.34.31937"

%struct.stack = type { i8, [256 x i64] }

$printf = comdat any

$__local_stdio_printf_options = comdat any

$"??_C@_0N@NBHKIKHE@Result?5?$CFllu?6?$AA@" = comdat any

$"?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA" = comdat any

@"??_C@_0N@NBHKIKHE@Result?5?$CFllu?6?$AA@" = linkonce_odr dso_local unnamed_addr constant [13 x i8] c"Result %llu\0A\00", comdat, align 1
@"?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA" = linkonce_odr dso_local global i64 0, comdat, align 8

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: readwrite) uwtable
define dso_local void @"?vm_push@@YAXPEAUstack@@_K@Z"(ptr nocapture noundef %0, i64 noundef %1) local_unnamed_addr #0 {
  %3 = load i8, ptr %0, align 8, !tbaa !5
  %4 = add i8 %3, 1
  store i8 %4, ptr %0, align 8, !tbaa !5
  %5 = zext i8 %4 to i64
  %6 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %5
  store i64 %1, ptr %6, align 8, !tbaa !9
  ret void
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: readwrite) uwtable
define dso_local noundef i64 @"?vm_pop@@YA_KPEAUstack@@@Z"(ptr nocapture noundef %0) local_unnamed_addr #0 {
  %2 = load i8, ptr %0, align 8, !tbaa !5
  %3 = add i8 %2, -1
  store i8 %3, ptr %0, align 8, !tbaa !5
  %4 = zext i8 %2 to i64
  %5 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %4
  %6 = load i64, ptr %5, align 8, !tbaa !9
  ret i64 %6
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: readwrite) uwtable
define dso_local void @"?vm_add@@YAXPEAUstack@@@Z"(ptr nocapture noundef %0) local_unnamed_addr #0 {
  %2 = load i8, ptr %0, align 8, !tbaa !5
  %3 = add i8 %2, -1
  %4 = zext i8 %2 to i64
  %5 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %4
  %6 = load i64, ptr %5, align 8, !tbaa !9
  %7 = zext i8 %3 to i64
  %8 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %7
  %9 = load i64, ptr %8, align 8, !tbaa !9
  %10 = add i64 %9, %6
  store i8 %3, ptr %0, align 8, !tbaa !5
  store i64 %10, ptr %8, align 8, !tbaa !9
  ret void
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(i64 immarg, ptr nocapture) #1

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(i64 immarg, ptr nocapture) #1

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: readwrite) uwtable
define dso_local void @"?vm_sub@@YAXPEAUstack@@@Z"(ptr nocapture noundef %0) local_unnamed_addr #0 {
  %2 = load i8, ptr %0, align 8, !tbaa !5
  %3 = add i8 %2, -1
  %4 = zext i8 %2 to i64
  %5 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %4
  %6 = load i64, ptr %5, align 8, !tbaa !9
  %7 = zext i8 %3 to i64
  %8 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %7
  %9 = load i64, ptr %8, align 8, !tbaa !9
  %10 = sub i64 %9, %6
  store i8 %3, ptr %0, align 8, !tbaa !5
  store i64 %10, ptr %8, align 8, !tbaa !9
  ret void
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: readwrite) uwtable
define dso_local void @"?vm_mul@@YAXPEAUstack@@@Z"(ptr nocapture noundef %0) local_unnamed_addr #0 {
  %2 = load i8, ptr %0, align 8, !tbaa !5
  %3 = add i8 %2, -1
  %4 = zext i8 %2 to i64
  %5 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %4
  %6 = load i64, ptr %5, align 8, !tbaa !9
  %7 = zext i8 %3 to i64
  %8 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %7
  %9 = load i64, ptr %8, align 8, !tbaa !9
  %10 = mul i64 %9, %6
  store i8 %3, ptr %0, align 8, !tbaa !5
  store i64 %10, ptr %8, align 8, !tbaa !9
  ret void
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: readwrite) uwtable
define dso_local void @"?vm_xor@@YAXPEAUstack@@@Z"(ptr nocapture noundef %0) local_unnamed_addr #0 {
  %2 = load i8, ptr %0, align 8, !tbaa !5
  %3 = add i8 %2, -1
  %4 = zext i8 %2 to i64
  %5 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %4
  %6 = load i64, ptr %5, align 8, !tbaa !9
  %7 = zext i8 %3 to i64
  %8 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %7
  %9 = load i64, ptr %8, align 8, !tbaa !9
  %10 = xor i64 %9, %6
  %11 = mul i64 %10, 5889
  %12 = add i64 %11, 4352
  store i8 %3, ptr %0, align 8, !tbaa !5
  store i64 %12, ptr %8, align 8, !tbaa !9
  ret void
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: readwrite) uwtable
define dso_local void @"?vm_and@@YAXPEAUstack@@@Z"(ptr nocapture noundef %0) local_unnamed_addr #0 {
  %2 = load i8, ptr %0, align 8, !tbaa !5
  %3 = add i8 %2, -1
  %4 = zext i8 %2 to i64
  %5 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %4
  %6 = load i64, ptr %5, align 8, !tbaa !9
  %7 = zext i8 %3 to i64
  %8 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %7
  %9 = load i64, ptr %8, align 8, !tbaa !9
  %10 = and i64 %9, %6
  store i8 %3, ptr %0, align 8, !tbaa !5
  store i64 %10, ptr %8, align 8, !tbaa !9
  ret void
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: readwrite) uwtable
define dso_local void @"?vm_or@@YAXPEAUstack@@@Z"(ptr nocapture noundef %0) local_unnamed_addr #0 {
  %2 = load i8, ptr %0, align 8, !tbaa !5
  %3 = add i8 %2, -1
  %4 = zext i8 %2 to i64
  %5 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %4
  %6 = load i64, ptr %5, align 8, !tbaa !9
  %7 = zext i8 %3 to i64
  %8 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %7
  %9 = load i64, ptr %8, align 8, !tbaa !9
  %10 = or i64 %9, %6
  store i8 %3, ptr %0, align 8, !tbaa !5
  store i64 %10, ptr %8, align 8, !tbaa !9
  ret void
}

; Function Attrs: mustprogress nofree nosync nounwind willreturn memory(argmem: read) uwtable
define dso_local noundef i64 @"?interpreter@@YA_KPEA_K@Z"(ptr nocapture noundef readonly %0) local_unnamed_addr #2 {
  %2 = alloca %struct.stack, align 8
  call void @llvm.lifetime.start.p0(i64 2056, ptr nonnull %2) #11
  %3 = load i64, ptr %0, align 8, !tbaa !9
  %4 = icmp eq i64 %3, 0
  br i1 %4, label %.loopexit, label %.preheader

.preheader:                                       ; preds = %1, %75
  %5 = phi i8 [ %76, %75 ], [ 0, %1 ]
  %6 = phi i64 [ %80, %75 ], [ %3, %1 ]
  %7 = phi i64 [ %78, %75 ], [ 0, %1 ]
  %8 = add i64 %6, -16
  %9 = tail call i64 @llvm.fshl.i64(i64 %6, i64 %8, i64 60)
  switch i64 %9, label %75 [
    i64 9, label %10
    i64 10, label %17
    i64 0, label %19
    i64 1, label %28
    i64 2, label %37
    i64 3, label %46
    i64 4, label %57
    i64 5, label %66
  ]

10:                                               ; preds = %.preheader
  %11 = add i64 %7, 1
  %12 = getelementptr inbounds i64, ptr %0, i64 %11
  %13 = load i64, ptr %12, align 8, !tbaa !9
  %14 = add i8 %5, 1
  %15 = zext i8 %14 to i64
  %16 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %15
  store i64 %13, ptr %16, align 8, !tbaa !9
  br label %75

17:                                               ; preds = %.preheader
  %18 = add i8 %5, -1
  br label %75

19:                                               ; preds = %.preheader
  %20 = add i8 %5, -1
  %21 = zext i8 %5 to i64
  %22 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %21
  %23 = load i64, ptr %22, align 8, !tbaa !9
  %24 = zext i8 %20 to i64
  %25 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %24
  %26 = load i64, ptr %25, align 8, !tbaa !9
  %27 = add i64 %26, %23
  store i64 %27, ptr %25, align 8, !tbaa !9
  br label %75

28:                                               ; preds = %.preheader
  %29 = add i8 %5, -1
  %30 = zext i8 %5 to i64
  %31 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %30
  %32 = load i64, ptr %31, align 8, !tbaa !9
  %33 = zext i8 %29 to i64
  %34 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %33
  %35 = load i64, ptr %34, align 8, !tbaa !9
  %36 = sub i64 %35, %32
  store i64 %36, ptr %34, align 8, !tbaa !9
  br label %75

37:                                               ; preds = %.preheader
  %38 = add i8 %5, -1
  %39 = zext i8 %5 to i64
  %40 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %39
  %41 = load i64, ptr %40, align 8, !tbaa !9
  %42 = zext i8 %38 to i64
  %43 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %42
  %44 = load i64, ptr %43, align 8, !tbaa !9
  %45 = mul i64 %44, %41
  store i64 %45, ptr %43, align 8, !tbaa !9
  br label %75

46:                                               ; preds = %.preheader
  %47 = add i8 %5, -1
  %48 = zext i8 %5 to i64
  %49 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %48
  %50 = load i64, ptr %49, align 8, !tbaa !9
  %51 = zext i8 %47 to i64
  %52 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %51
  %53 = load i64, ptr %52, align 8, !tbaa !9
  %54 = xor i64 %53, %50
  %55 = mul i64 %54, 5889
  %56 = add i64 %55, 4352
  store i64 %56, ptr %52, align 8, !tbaa !9
  br label %75

57:                                               ; preds = %.preheader
  %58 = add i8 %5, -1
  %59 = zext i8 %5 to i64
  %60 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %59
  %61 = load i64, ptr %60, align 8, !tbaa !9
  %62 = zext i8 %58 to i64
  %63 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %62
  %64 = load i64, ptr %63, align 8, !tbaa !9
  %65 = and i64 %64, %61
  store i64 %65, ptr %63, align 8, !tbaa !9
  br label %75

66:                                               ; preds = %.preheader
  %67 = add i8 %5, -1
  %68 = zext i8 %5 to i64
  %69 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %68
  %70 = load i64, ptr %69, align 8, !tbaa !9
  %71 = zext i8 %67 to i64
  %72 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %71
  %73 = load i64, ptr %72, align 8, !tbaa !9
  %74 = or i64 %73, %70
  store i64 %74, ptr %72, align 8, !tbaa !9
  br label %75

75:                                               ; preds = %66, %57, %46, %37, %28, %19, %17, %10, %.preheader
  %76 = phi i8 [ %5, %.preheader ], [ %67, %66 ], [ %58, %57 ], [ %47, %46 ], [ %38, %37 ], [ %29, %28 ], [ %20, %19 ], [ %18, %17 ], [ %14, %10 ]
  %77 = phi i64 [ %7, %.preheader ], [ %7, %66 ], [ %7, %57 ], [ %7, %46 ], [ %7, %37 ], [ %7, %28 ], [ %7, %19 ], [ %7, %17 ], [ %11, %10 ]
  %78 = add i64 %77, 1
  %79 = getelementptr inbounds i64, ptr %0, i64 %78
  %80 = load i64, ptr %79, align 8, !tbaa !9
  %81 = icmp eq i64 %80, 0
  br i1 %81, label %.loopexit, label %.preheader, !llvm.loop !11

.loopexit:                                        ; preds = %75, %1
  %82 = phi i8 [ 0, %1 ], [ %76, %75 ]
  %83 = zext i8 %82 to i64
  %84 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %83
  %85 = load i64, ptr %84, align 8, !tbaa !9
  call void @llvm.lifetime.end.p0(i64 2056, ptr nonnull %2) #11
  ret i64 %85
}

; Function Attrs: mustprogress nofree nosync nounwind willreturn memory(none) uwtable
define dso_local noundef i64 @"?vmSecretComputation@@YA_K_K0@Z"(i64 noundef %0, i64 noundef %1) local_unnamed_addr #3 {
  %3 = alloca [20 x i64], align 16
  call void @llvm.lifetime.start.p0(i64 160, ptr nonnull %3) #11
  store <2 x i64> <i64 160, i64 4>, ptr %3, align 16, !tbaa !9
  %4 = getelementptr inbounds i64, ptr %3, i64 2
  store i64 160, ptr %4, align 16, !tbaa !9
  %5 = getelementptr inbounds i64, ptr %3, i64 3
  store i64 %0, ptr %5, align 8, !tbaa !9
  %6 = getelementptr inbounds i64, ptr %3, i64 4
  store i64 160, ptr %6, align 16, !tbaa !9
  %7 = getelementptr inbounds i64, ptr %3, i64 5
  store i64 %1, ptr %7, align 8, !tbaa !9
  %8 = getelementptr inbounds i64, ptr %3, i64 6
  store <2 x i64> <i64 16, i64 160>, ptr %8, align 16, !tbaa !9
  %9 = getelementptr inbounds i64, ptr %3, i64 8
  store i64 %0, ptr %9, align 16, !tbaa !9
  %10 = getelementptr inbounds i64, ptr %3, i64 9
  store i64 160, ptr %10, align 8, !tbaa !9
  %11 = getelementptr inbounds i64, ptr %3, i64 10
  store i64 %1, ptr %11, align 16, !tbaa !9
  %12 = getelementptr inbounds i64, ptr %3, i64 11
  store <2 x i64> <i64 80, i64 160>, ptr %12, align 8, !tbaa !9
  %13 = getelementptr inbounds i64, ptr %3, i64 13
  store i64 %0, ptr %13, align 8, !tbaa !9
  %14 = getelementptr inbounds i64, ptr %3, i64 14
  store i64 160, ptr %14, align 16, !tbaa !9
  %15 = getelementptr inbounds i64, ptr %3, i64 15
  store i64 %1, ptr %15, align 8, !tbaa !9
  %16 = getelementptr inbounds i64, ptr %3, i64 16
  store <2 x i64> <i64 96, i64 64>, ptr %16, align 16, !tbaa !9
  %17 = getelementptr inbounds i64, ptr %3, i64 18
  store <2 x i64> <i64 32, i64 48>, ptr %17, align 16, !tbaa !9
  %18 = call noundef i64 @"?interpreter@@YA_KPEA_K@Z"(ptr noundef nonnull %3)
  call void @llvm.lifetime.end.p0(i64 160, ptr nonnull %3) #11
  ret i64 %18
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable
define dso_local noundef i64 @"?secretComputation@@YA_K_K0@Z"(i64 noundef %0, i64 noundef %1) local_unnamed_addr #4 {
  %3 = and i64 %1, %0
  %4 = shl i64 %3, 3
  ret i64 %4
}

; Function Attrs: mustprogress norecurse uwtable
define dso_local noundef i32 @main() local_unnamed_addr #5 {
  %1 = alloca [20 x i64], align 16
  call void @llvm.lifetime.start.p0(i64 160, ptr nonnull %1) #11
  store <2 x i64> <i64 160, i64 4>, ptr %1, align 16, !tbaa !9
  %2 = getelementptr inbounds i64, ptr %1, i64 2
  store <2 x i64> <i64 160, i64 1234>, ptr %2, align 16, !tbaa !9
  %3 = getelementptr inbounds i64, ptr %1, i64 4
  store <2 x i64> <i64 160, i64 5678>, ptr %3, align 16, !tbaa !9
  %4 = getelementptr inbounds i64, ptr %1, i64 6
  store <2 x i64> <i64 16, i64 160>, ptr %4, align 16, !tbaa !9
  %5 = getelementptr inbounds i64, ptr %1, i64 8
  store <2 x i64> <i64 1234, i64 160>, ptr %5, align 16, !tbaa !9
  %6 = getelementptr inbounds i64, ptr %1, i64 10
  store <2 x i64> <i64 5678, i64 80>, ptr %6, align 16, !tbaa !9
  %7 = getelementptr inbounds i64, ptr %1, i64 12
  store <2 x i64> <i64 160, i64 1234>, ptr %7, align 16, !tbaa !9
  %8 = getelementptr inbounds i64, ptr %1, i64 14
  store <2 x i64> <i64 160, i64 5678>, ptr %8, align 16, !tbaa !9
  %9 = getelementptr inbounds i64, ptr %1, i64 16
  store <2 x i64> <i64 96, i64 64>, ptr %9, align 16, !tbaa !9
  %10 = getelementptr inbounds i64, ptr %1, i64 18
  store <2 x i64> <i64 32, i64 48>, ptr %10, align 16, !tbaa !9
  %11 = call noundef i64 @"?interpreter@@YA_KPEA_K@Z"(ptr noundef nonnull %1)
  call void @llvm.lifetime.end.p0(i64 160, ptr nonnull %1) #11
  %12 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @"??_C@_0N@NBHKIKHE@Result?5?$CFllu?6?$AA@", i64 noundef %11)
  ret i32 0
}

; Function Attrs: inlinehint mustprogress uwtable
define linkonce_odr dso_local i32 @printf(ptr noundef %0, ...) local_unnamed_addr #6 comdat {
  %2 = alloca ptr, align 8
  call void @llvm.lifetime.start.p0(i64 8, ptr nonnull %2) #11
  call void @llvm.va_start(ptr nonnull %2)
  %3 = load ptr, ptr %2, align 8, !tbaa !13
  %4 = call ptr @__acrt_iob_func(i32 noundef 1)
  %5 = call ptr @__local_stdio_printf_options()
  %6 = load i64, ptr %5, align 8, !tbaa !9
  %7 = call i32 @__stdio_common_vfprintf(i64 noundef %6, ptr noundef %4, ptr noundef %0, ptr noundef null, ptr noundef %3)
  call void @llvm.va_end(ptr nonnull %2)
  call void @llvm.lifetime.end.p0(i64 8, ptr nonnull %2) #11
  ret i32 %7
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.va_start(ptr) #7

declare dso_local ptr @__acrt_iob_func(i32 noundef) local_unnamed_addr #8

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.va_end(ptr) #7

declare dso_local i32 @__stdio_common_vfprintf(i64 noundef, ptr noundef, ptr noundef, ptr noundef, ptr noundef) local_unnamed_addr #8

; Function Attrs: mustprogress noinline nounwind uwtable
define linkonce_odr dso_local ptr @__local_stdio_printf_options() local_unnamed_addr #9 comdat {
  ret ptr @"?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA"
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i64 @llvm.fshl.i64(i64, i64, i64) #10

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: readwrite) uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { mustprogress nofree nosync nounwind willreturn memory(argmem: read) uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { mustprogress nofree nosync nounwind willreturn memory(none) uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #5 = { mustprogress norecurse uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #6 = { inlinehint mustprogress uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #7 = { mustprogress nocallback nofree nosync nounwind willreturn }
attributes #8 = { "frame-pointer"="none" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #9 = { mustprogress noinline nounwind uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #10 = { mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #11 = { nounwind }

!llvm.linker.options = !{!0}
!llvm.module.flags = !{!1, !2, !3}
!llvm.ident = !{!4}

!0 = !{!"/FAILIFMISMATCH:\22_CRT_STDIO_ISO_WIDE_SPECIFIERS=0\22"}
!1 = !{i32 1, !"wchar_size", i32 2}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{!"clang version 15.0.0"}
!5 = !{!6, !7, i64 0}
!6 = !{!"?AUstack@@", !7, i64 0, !7, i64 8}
!7 = !{!"omnipotent char", !8, i64 0}
!8 = !{!"Simple C++ TBAA"}
!9 = !{!10, !10, i64 0}
!10 = !{!"long long", !7, i64 0}
!11 = distinct !{!11, !12}
!12 = !{!"llvm.loop.mustprogress"}
!13 = !{!14, !14, i64 0}
!14 = !{!"any pointer", !7, i64 0}
