; (x & C) + (x | (C + 1)) with x = 1234 * a is 1234 * a + C + 1. A simplifier
; may answer 1234 * (a & 0x7fffffffffffffff) + C + 1, which is the same (the
; top bit of a is multiplied out) but not as simple: the mask goes.
define i64 @masked(i64 %a) {
  %x = mul i64 %a, 1234
  %l = and i64 %x, 153237470
  %h = or i64 %x, 153237471
  %s = add i64 %l, %h
  ret i64 %s
}

define i64 @plain(i64 %a) {
  %m = and i64 %a, 9223372036854775807
  %x = mul i64 %m, 1234
  ret i64 %x
}
