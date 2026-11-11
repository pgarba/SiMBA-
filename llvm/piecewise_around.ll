; From an obfuscated crackme, lifted with remill: with K = 0x1828d12345b71 and
; t = sx8(u) * 10 - 528 + sx8(v),
;   ((t >> 1) ^ 0x8000c146891a2db8) - ((((v & 1) | (K-1)) - t) >> 1)
;     - (t | ~K) + (t u< K ? 0x7fffffffffffffff : -1)
; is t ^ K ^ (t == K ? 1<<63 : 0). Two things are in the way: (v & 1) reads
; v around t (it is t & 1, as 10 * sx8(u) - 528 is even), and the step is at
; a single point, which no sample hits.
define i64 @around(i8 %u, i8 %v) {
  %su = sext i8 %u to i64
  %sv = sext i8 %v to i64
  %m10 = mul i64 %su, 10
  %a = add i64 %m10, -528
  %t = add i64 %a, %sv
  %s = lshr i64 %t, 1
  %x = xor i64 %s, -9223159528162710088
  %v1 = and i8 %v, 1
  %vz = zext i8 %v1 to i64
  %o = or i64 %vz, 425017384131440
  %d = sub i64 %o, %t
  %e = lshr i64 %d, 1
  %n = or i64 %t, -425017384131442
  %f = add i64 %e, %n
  %g = sub i64 %x, %f
  %c = icmp ult i64 %t, 425017384131441
  %k = select i1 %c, i64 9223372036854775807, i64 -1
  %r = add i64 %g, %k
  ret i64 %r
}
