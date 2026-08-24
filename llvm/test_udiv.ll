source_filename = "test_udiv"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

define i8 @f_udiv(i8 noundef %a, i8 noundef %b) {
  %1 = udiv i8 %a, %b
  ret i8 %1
}
