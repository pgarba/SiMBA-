; A pointer through an average with the carry, as an obfuscator's rcr
; lifts (remill): ceil(p / 2) + floor(p / 2). No bitwise identity: the tree
; has a comparison and a funnel shift. Guessed as p on samples, proved by Z3.
define i64 @avg(i64 %p, ptr %q) {
  %o = and i64 %p, 1
  %s = add i64 %o, %p
  %c = icmp eq i64 %p, -1
  %z = zext i1 %c to i64
  %h = lshr i64 %p, 1
  %f = tail call i64 @llvm.fshl.i64(i64 %z, i64 %s, i64 63)
  %r = add i64 %f, %h
  store i64 %r, ptr %q, align 8
  ret i64 %r
}

declare i64 @llvm.fshl.i64(i64, i64, i64)
