; A sum whose terms cancel across a value with another use (from an
; obfuscated crackme, lifted with remill): %d is also stored, so LLVM does
; not reassociate (l + m) - (l - s) - 48, which is m + s - 48 without %l.
define i64 @cancel(i64 %l, i64 %m, i64 %s, ptr %p) {
  %d = sub i64 %l, %s
  %d48 = add i64 %d, 48
  store i64 %d48, ptr %p, align 8
  %e = add i64 %l, %m
  %r = sub i64 %e, %d
  %r48 = add i64 %r, -48
  ret i64 %r48
}
