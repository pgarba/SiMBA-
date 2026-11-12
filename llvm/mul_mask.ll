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

; the same mask after LLVM moved it through a bswap and into the constant:
; bit 7 of x is the product's dropped top bit
define i64 @swapped(i64 %x) {
  %m = and i64 %x, -129
  %k = xor i64 %m, 425017384131569
  %b = tail call i64 @llvm.bswap.i64(i64 %k)
  %p = mul i64 %b, 1234
  ret i64 %p
}

declare i64 @llvm.bswap.i64(i64)
