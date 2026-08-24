source_filename = "test_lshr"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

define i8 @f_lshr(i8 noundef %a) {
  %1 = lshr i8 %a, 3
  ret i8 %1
}
