; ModuleID = 'vm_obf.c'
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

; Function Attrs: argmemonly mustprogress nofree norecurse nosync nounwind willreturn uwtable
define dso_local void @"?vm_push@@YAXPEAUstack@@_K@Z"(ptr nocapture noundef %0, i64 noundef %1) local_unnamed_addr #0 {
  %3 = load i8, ptr %0, align 8, !tbaa !5
  %4 = add i8 %3, 1
  store i8 %4, ptr %0, align 8, !tbaa !5
  %5 = zext i8 %4 to i64
  %6 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %5
  store i64 %1, ptr %6, align 8, !tbaa !9
  ret void
}

; Function Attrs: argmemonly mustprogress nofree norecurse nosync nounwind willreturn uwtable
define dso_local noundef i64 @"?vm_pop@@YA_KPEAUstack@@@Z"(ptr nocapture noundef %0) local_unnamed_addr #0 {
  %2 = load i8, ptr %0, align 8, !tbaa !5
  %3 = add i8 %2, -1
  store i8 %3, ptr %0, align 8, !tbaa !5
  %4 = zext i8 %2 to i64
  %5 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %4
  %6 = load i64, ptr %5, align 8, !tbaa !9
  ret i64 %6
}

; Function Attrs: argmemonly mustprogress nofree norecurse nosync nounwind willreturn uwtable
define dso_local void @"?vm_add@@YAXPEAUstack@@@Z"(ptr nocapture noundef %0) local_unnamed_addr #0 {
  %2 = load i8, ptr %0, align 8, !tbaa !5
  %3 = add i8 %2, -1
  %4 = zext i8 %2 to i64
  %5 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %4
  %6 = load i64, ptr %5, align 8, !tbaa !9
  %7 = zext i8 %3 to i64
  %8 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %7
  %9 = load i64, ptr %8, align 8, !tbaa !9
  %10 = and i64 %9, %6
  %11 = mul i64 %10, 3
  %12 = or i64 %9, %6
  %13 = sub i64 %11, %12
  %14 = xor i64 %9, %6
  %15 = shl i64 %14, 1
  %16 = add i64 %13, %15
  store i8 %3, ptr %0, align 8, !tbaa !5
  store i64 %16, ptr %8, align 8, !tbaa !9
  ret void
}

; Function Attrs: argmemonly mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.lifetime.start.p0(i64 immarg, ptr nocapture) #1

; Function Attrs: argmemonly mustprogress nocallback nofree nosync nounwind willreturn
declare void @llvm.lifetime.end.p0(i64 immarg, ptr nocapture) #1

; Function Attrs: argmemonly mustprogress nofree norecurse nosync nounwind willreturn uwtable
define dso_local void @"?vm_sub@@YAXPEAUstack@@@Z"(ptr nocapture noundef %0) local_unnamed_addr #0 {
  %2 = load i8, ptr %0, align 8, !tbaa !5
  %3 = add i8 %2, -1
  %4 = zext i8 %2 to i64
  %5 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %4
  %6 = load i64, ptr %5, align 8, !tbaa !9
  %7 = zext i8 %3 to i64
  %8 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %7
  %9 = load i64, ptr %8, align 8, !tbaa !9
  %10 = and i64 %9, %6
  %11 = mul i64 %10, -3
  %12 = xor i64 %9, %6
  %13 = or i64 %9, %6
  %14 = sub i64 %9, %12
  %15 = shl i64 %14, 1
  %16 = add i64 %11, %13
  %17 = add i64 %16, %15
  store i8 %3, ptr %0, align 8, !tbaa !5
  store i64 %17, ptr %8, align 8, !tbaa !9
  ret void
}

; Function Attrs: argmemonly mustprogress nofree norecurse nosync nounwind willreturn uwtable
define dso_local void @"?vm_mul@@YAXPEAUstack@@@Z"(ptr nocapture noundef %0) local_unnamed_addr #0 {
  %2 = load i8, ptr %0, align 8, !tbaa !5
  %3 = add i8 %2, -1
  %4 = zext i8 %2 to i64
  %5 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %4
  %6 = load i64, ptr %5, align 8, !tbaa !9
  %7 = zext i8 %3 to i64
  %8 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %7
  %9 = load i64, ptr %8, align 8, !tbaa !9
  %10 = xor i64 %9, %6
  %11 = mul i64 %9, %6
  %12 = xor i64 %6, -1
  %13 = or i64 %9, %12
  %14 = shl i64 %13, 1
  %15 = add i64 %6, 2
  %16 = sub i64 %15, %9
  %17 = add i64 %16, %10
  %18 = add i64 %17, %11
  %19 = add i64 %18, %14
  store i8 %3, ptr %0, align 8, !tbaa !5
  store i64 %19, ptr %8, align 8, !tbaa !9
  ret void
}

; Function Attrs: argmemonly mustprogress nofree norecurse nosync nounwind willreturn uwtable
define dso_local void @"?vm_xor@@YAXPEAUstack@@@Z"(ptr nocapture noundef %0) local_unnamed_addr #0 {
  %2 = load i8, ptr %0, align 8, !tbaa !5
  %3 = add i8 %2, -1
  %4 = zext i8 %2 to i64
  %5 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %4
  %6 = load i64, ptr %5, align 8, !tbaa !9
  %7 = zext i8 %3 to i64
  %8 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %7
  %9 = load i64, ptr %8, align 8, !tbaa !9
  %10 = sub i64 %9, %6
  %11 = xor i64 %6, -1
  %12 = or i64 %9, %11
  %13 = mul i64 %12, -2
  %14 = add i64 %10, %13
  %15 = mul i64 %14, 5889
  %16 = add i64 %15, -7426
  store i8 %3, ptr %0, align 8, !tbaa !5
  store i64 %16, ptr %8, align 8, !tbaa !9
  ret void
}

; Function Attrs: argmemonly mustprogress nofree norecurse nosync nounwind willreturn uwtable
define dso_local void @"?vm_and@@YAXPEAUstack@@@Z"(ptr nocapture noundef %0) local_unnamed_addr #0 {
  %2 = load i8, ptr %0, align 8, !tbaa !5
  %3 = add i8 %2, -1
  %4 = zext i8 %2 to i64
  %5 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %4
  %6 = load i64, ptr %5, align 8, !tbaa !9
  %7 = zext i8 %3 to i64
  %8 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %7
  %9 = load i64, ptr %8, align 8, !tbaa !9
  %10 = or i64 %9, %6
  %11 = sub i64 %9, %10
  %12 = shl i64 %11, 1
  %13 = sub i64 %6, %9
  %14 = add i64 %13, %10
  %15 = add i64 %14, %12
  store i8 %3, ptr %0, align 8, !tbaa !5
  store i64 %15, ptr %8, align 8, !tbaa !9
  ret void
}

; Function Attrs: argmemonly mustprogress nofree norecurse nosync nounwind willreturn uwtable
define dso_local void @"?vm_or@@YAXPEAUstack@@@Z"(ptr nocapture noundef %0) local_unnamed_addr #0 {
  %2 = load i8, ptr %0, align 8, !tbaa !5
  %3 = add i8 %2, -1
  %4 = zext i8 %2 to i64
  %5 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %4
  %6 = load i64, ptr %5, align 8, !tbaa !9
  %7 = zext i8 %3 to i64
  %8 = getelementptr inbounds %struct.stack, ptr %0, i64 0, i32 1, i64 %7
  %9 = load i64, ptr %8, align 8, !tbaa !9
  %10 = and i64 %9, %6
  %11 = mul i64 %10, 3
  %12 = add i64 %9, %6
  %13 = sub i64 %11, %12
  %14 = xor i64 %9, %6
  %15 = shl i64 %14, 1
  %16 = add i64 %13, %15
  store i8 %3, ptr %0, align 8, !tbaa !5
  store i64 %16, ptr %8, align 8, !tbaa !9
  ret void
}

; Function Attrs: argmemonly mustprogress nofree nosync nounwind readonly willreturn uwtable
define dso_local noundef i64 @"?interpreter@@YA_KPEA_K@Z"(ptr nocapture noundef readonly %0) local_unnamed_addr #2 {
  %2 = alloca %struct.stack, align 8
  call void @llvm.lifetime.start.p0(i64 2056, ptr nonnull %2) #11
  %3 = load i64, ptr %0, align 8, !tbaa !9
  %4 = icmp eq i64 %3, 0
  br i1 %4, label %120, label %5

5:                                                ; preds = %1, %113
  %6 = phi i8 [ %114, %113 ], [ 0, %1 ]
  %7 = phi i64 [ %118, %113 ], [ %3, %1 ]
  %8 = phi i64 [ %116, %113 ], [ 0, %1 ]
  %9 = add i64 %7, -16
  %10 = tail call i64 @llvm.fshl.i64(i64 %9, i64 %9, i64 60)
  switch i64 %10, label %113 [
    i64 9, label %11
    i64 10, label %18
    i64 0, label %20
    i64 1, label %35
    i64 2, label %51
    i64 3, label %69
    i64 4, label %84
    i64 5, label %98
  ]

11:                                               ; preds = %5
  %12 = add i64 %8, 1
  %13 = getelementptr inbounds i64, ptr %0, i64 %12
  %14 = load i64, ptr %13, align 8, !tbaa !9
  %15 = add i8 %6, 1
  store i8 %15, ptr %2, align 8, !tbaa !5
  %16 = zext i8 %15 to i64
  %17 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %16
  store i64 %14, ptr %17, align 8, !tbaa !9
  br label %113

18:                                               ; preds = %5
  %19 = add i8 %6, -1
  store i8 %19, ptr %2, align 8, !tbaa !5
  br label %113

20:                                               ; preds = %5
  %21 = add i8 %6, -1
  %22 = zext i8 %6 to i64
  %23 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %22
  %24 = load i64, ptr %23, align 8, !tbaa !9
  %25 = zext i8 %21 to i64
  %26 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %25
  %27 = load i64, ptr %26, align 8, !tbaa !9
  %28 = and i64 %27, %24
  %29 = mul i64 %28, 3
  %30 = or i64 %27, %24
  %31 = sub i64 %29, %30
  %32 = xor i64 %27, %24
  %33 = shl i64 %32, 1
  %34 = add i64 %31, %33
  store i8 %21, ptr %2, align 8, !tbaa !5
  store i64 %34, ptr %26, align 8, !tbaa !9
  br label %113

35:                                               ; preds = %5
  %36 = add i8 %6, -1
  %37 = zext i8 %6 to i64
  %38 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %37
  %39 = load i64, ptr %38, align 8, !tbaa !9
  %40 = zext i8 %36 to i64
  %41 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %40
  %42 = load i64, ptr %41, align 8, !tbaa !9
  %43 = and i64 %42, %39
  %44 = mul i64 %43, -3
  %45 = xor i64 %42, %39
  %46 = or i64 %42, %39
  %47 = sub i64 %42, %45
  %48 = shl i64 %47, 1
  %49 = add i64 %44, %46
  %50 = add i64 %49, %48
  store i8 %36, ptr %2, align 8, !tbaa !5
  store i64 %50, ptr %41, align 8, !tbaa !9
  br label %113

51:                                               ; preds = %5
  %52 = add i8 %6, -1
  %53 = zext i8 %6 to i64
  %54 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %53
  %55 = load i64, ptr %54, align 8, !tbaa !9
  %56 = zext i8 %52 to i64
  %57 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %56
  %58 = load i64, ptr %57, align 8, !tbaa !9
  %59 = xor i64 %58, %55
  %60 = mul i64 %58, %55
  %61 = xor i64 %55, -1
  %62 = or i64 %58, %61
  %63 = shl i64 %62, 1
  %64 = add i64 %55, 2
  %65 = sub i64 %64, %58
  %66 = add i64 %65, %59
  %67 = add i64 %66, %60
  %68 = add i64 %67, %63
  store i8 %52, ptr %2, align 8, !tbaa !5
  store i64 %68, ptr %57, align 8, !tbaa !9
  br label %113

69:                                               ; preds = %5
  %70 = add i8 %6, -1
  %71 = zext i8 %6 to i64
  %72 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %71
  %73 = load i64, ptr %72, align 8, !tbaa !9
  %74 = zext i8 %70 to i64
  %75 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %74
  %76 = load i64, ptr %75, align 8, !tbaa !9
  %77 = sub i64 %76, %73
  %78 = xor i64 %73, -1
  %79 = or i64 %76, %78
  %80 = mul i64 %79, -2
  %81 = add i64 %77, %80
  %82 = mul i64 %81, 5889
  %83 = add i64 %82, -7426
  store i8 %70, ptr %2, align 8, !tbaa !5
  store i64 %83, ptr %75, align 8, !tbaa !9
  br label %113

84:                                               ; preds = %5
  %85 = add i8 %6, -1
  %86 = zext i8 %6 to i64
  %87 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %86
  %88 = load i64, ptr %87, align 8, !tbaa !9
  %89 = zext i8 %85 to i64
  %90 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %89
  %91 = load i64, ptr %90, align 8, !tbaa !9
  %92 = or i64 %91, %88
  %93 = sub i64 %91, %92
  %94 = shl i64 %93, 1
  %95 = sub i64 %88, %91
  %96 = add i64 %95, %92
  %97 = add i64 %96, %94
  store i8 %85, ptr %2, align 8, !tbaa !5
  store i64 %97, ptr %90, align 8, !tbaa !9
  br label %113

98:                                               ; preds = %5
  %99 = add i8 %6, -1
  %100 = zext i8 %6 to i64
  %101 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %100
  %102 = load i64, ptr %101, align 8, !tbaa !9
  %103 = zext i8 %99 to i64
  %104 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %103
  %105 = load i64, ptr %104, align 8, !tbaa !9
  %106 = and i64 %105, %102
  %107 = mul i64 %106, 3
  %108 = add i64 %105, %102
  %109 = sub i64 %107, %108
  %110 = xor i64 %105, %102
  %111 = shl i64 %110, 1
  %112 = add i64 %109, %111
  store i8 %99, ptr %2, align 8, !tbaa !5
  store i64 %112, ptr %104, align 8, !tbaa !9
  br label %113

113:                                              ; preds = %5, %98, %84, %69, %51, %35, %20, %18, %11
  %114 = phi i8 [ %6, %5 ], [ %99, %98 ], [ %85, %84 ], [ %70, %69 ], [ %52, %51 ], [ %36, %35 ], [ %21, %20 ], [ %19, %18 ], [ %15, %11 ]
  %115 = phi i64 [ %8, %5 ], [ %8, %98 ], [ %8, %84 ], [ %8, %69 ], [ %8, %51 ], [ %8, %35 ], [ %8, %20 ], [ %8, %18 ], [ %12, %11 ]
  %116 = add i64 %115, 1
  %117 = getelementptr inbounds i64, ptr %0, i64 %116
  %118 = load i64, ptr %117, align 8, !tbaa !9
  %119 = icmp eq i64 %118, 0
  br i1 %119, label %120, label %5, !llvm.loop !11

120:                                              ; preds = %113, %1
  %121 = phi i8 [ 0, %1 ], [ %114, %113 ]
  %122 = zext i8 %121 to i64
  %123 = getelementptr inbounds %struct.stack, ptr %2, i64 0, i32 1, i64 %122
  %124 = load i64, ptr %123, align 8, !tbaa !9
  call void @llvm.lifetime.end.p0(i64 2056, ptr nonnull %2) #11
  ret i64 %124
}

; Function Attrs: mustprogress nofree nosync nounwind readnone willreturn uwtable
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

; Function Attrs: mustprogress nofree norecurse nosync nounwind readnone willreturn uwtable
define dso_local noundef i64 @"?secretComputation@@YA_K_K0@Z"(i64 noundef %0, i64 noundef %1) local_unnamed_addr #4 {
  %3 = add i64 %1, %0
  %4 = xor i64 %1, %0
  %5 = sub i64 %3, %4
  %6 = shl i64 %5, 2
  ret i64 %6
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
  %12 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull @"??_C@_0N@NBHKIKHE@Result?5?$CFllu?6?$AA@", i64 noundef %11)
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

; Function Attrs: nocallback nofree nosync nounwind readnone speculatable willreturn
declare i64 @llvm.fshl.i64(i64, i64, i64) #10

attributes #0 = { argmemonly mustprogress nofree norecurse nosync nounwind willreturn uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { argmemonly mustprogress nocallback nofree nosync nounwind willreturn }
attributes #2 = { argmemonly mustprogress nofree nosync nounwind readonly willreturn uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { mustprogress nofree nosync nounwind readnone willreturn uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { mustprogress nofree norecurse nosync nounwind readnone willreturn uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #5 = { mustprogress norecurse uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #6 = { inlinehint mustprogress uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #7 = { mustprogress nocallback nofree nosync nounwind willreturn }
attributes #8 = { "frame-pointer"="none" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #9 = { mustprogress noinline nounwind uwtable "frame-pointer"="none" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #10 = { nocallback nofree nosync nounwind readnone speculatable willreturn }
attributes #11 = { nounwind }

!llvm.linker.options = !{!0}
!llvm.module.flags = !{!1, !2, !3}
!llvm.ident = !{!4}

!0 = !{!"/FAILIFMISMATCH:\22_CRT_STDIO_ISO_WIDE_SPECIFIERS=0\22"}
!1 = !{i32 1, !"wchar_size", i32 2}
!2 = !{i32 7, !"PIC Level", i32 2}
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
