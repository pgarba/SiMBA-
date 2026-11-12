; A digit's value the long way round (from an obfuscated crackme, lifted
; with remill): the byte is sign-extended in two halves and the 48 comes in
; through an or / and / xor of 0xcf and a subtraction. Every value of the
; byte is tried: it is sext(v) - 48.
define i64 @digit(i8 %v) {
  %o = or i8 %v, -49
  %so = sext i8 %o to i32
  %neg = icmp slt i8 %v, 0
  %hi = select i1 %neg, i32 -256, i32 0
  %a = and i8 %v, -49
  %x = xor i8 %a, -49
  %zx = zext i8 %x to i32
  %lo = or disjoint i32 %hi, %zx
  %m = xor i32 %lo, -208
  %d = sub i32 %so, %m
  %r = sext i32 %d to i64
  ret i64 %r
}

; the sign extension as two terms of a sum, among others: (z & 0x7f) -
; (z & 0x80) with z = zext v is sext(v)
define i64 @split(i8 %v, i64 %a) {
  %z = zext i8 %v to i64
  %lo = and i64 %z, 127
  %hi = and i64 %z, 128
  %t = mul i64 %a, 10
  %u = add i64 %t, -528
  %w = add i64 %u, %lo
  %r = sub i64 %w, %hi
  ret i64 %r
}
