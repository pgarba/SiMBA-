; verify() tests candidates on random inputs, which essentially never reach
; a comparison boundary: "0" passed for select(x u<= 1000000, 64, 0), and
; @trap became x ^ 12345. It must keep its comparisons (or be simplified
; correctly); @mba must still be simplified.
define i64 @trap(i64 %x) {
  %c = icmp ule i64 %x, 1000000
  %s = select i1 %c, i64 64, i64 0
  %h = icmp eq i64 %x, 777777
  %s2 = select i1 %h, i64 1, i64 %s
  %y = xor i64 %s2, 12345
  %z = add i64 %y, 0
  %r = xor i64 %x, %z
  ret i64 %r
}

define i64 @mba(i64 %x, i64 %y) {
  %a = xor i64 %x, %y
  %b = and i64 %x, %y
  %c = shl i64 %b, 1
  %r = add i64 %a, %c
  ret i64 %r
}
