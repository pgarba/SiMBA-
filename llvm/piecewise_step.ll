; A step function hidden as a borrow (from an obfuscated crackme, lifted with
; remill): with K = 0x1828d12345b71,
;   ((t & K) ^ K) + ((t >> 1) ^ ((K >> 1) | 1<<63)) - (((K-1) - (t & ~1)) >> 1)
; is t ^ K ^ (t <=u K ? 1<<63 : 0). No MBA simplifier can return that (it
; needs a comparison); the piecewise stage finds it and proves it. t is
; computed from two variables, so the step's variable is a cut of the tree,
; not a variable of the candidate.
define i64 @step(i64 %x, i64 %y) {
  %m10 = mul i64 %x, 10
  %t = add i64 %m10, %y
  %a = and i64 %t, 425017384131441
  %b = xor i64 %a, 425017384131441
  %s = lshr i64 %t, 1
  %c = xor i64 %s, -9223159528162710088
  %m = and i64 %t, -2
  %d = sub i64 425017384131440, %m
  %e = lshr i64 %d, 1
  %f = add i64 %b, %c
  %r = sub i64 %f, %e
  ret i64 %r
}
