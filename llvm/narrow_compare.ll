; A null check on a byte the long way round (from an obfuscated crackme,
; lifted with remill): the byte is widened, shifted up and down again, and
; its two halves put back together before the comparison with zero. Every
; value of the byte is tried: the comparison is v == 0.
define i1 @null_byte(i8 %v) {
  %z = zext i8 %v to i64
  %s = shl nuw i64 %z, 56
  %a = ashr exact i64 %s, 55
  %l = lshr exact i64 %a, 1
  %hi = and i64 %a, 4294967040
  %lo = and i64 %l, 255
  %o = or disjoint i64 %lo, %hi
  %c = icmp eq i64 %o, 0
  ret i1 %c
}
