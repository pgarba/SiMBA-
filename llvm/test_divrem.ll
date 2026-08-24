; Test module for the >> / / % (lshr / udiv / urem) operator support.
; 8-bit pure-arithmetic functions so the general simplifier stays fast.
;   - f_lshr : lshr by a constant power of two  -> Tier 1 bit desugar
;   - f_udiv : udiv by a variable              -> Tier 2 first-class node
;   - f_urem : urem by a variable              -> Tier 2 first-class node
;   - f_mix  : a mix of all three
source_filename = "test_divrem"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

define i8 @f_lshr(i8 noundef %a) {
  %1 = lshr i8 %a, 3
  ret i8 %1
}

define i8 @f_udiv(i8 noundef %a, i8 noundef %b) {
  %1 = udiv i8 %a, %b
  ret i8 %1
}

define i8 @f_urem(i8 noundef %a, i8 noundef %b) {
  %1 = urem i8 %a, %b
  ret i8 %1
}

define i8 @f_mix(i8 noundef %a, i8 noundef %b) {
  %1 = lshr i8 %a, 2
  %2 = udiv i8 %a, %b
  %3 = urem i8 %a, %b
  %4 = add i8 %1, %2
  %5 = add i8 %4, %3
  ret i8 %5
}
