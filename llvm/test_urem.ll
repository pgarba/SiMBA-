; 8-bit unsigned remainder (urem) test for the SiMBA++ --ir path.
; f_urem(a, b) = a % b   (unsigned, mod 256)
define i8 @f_urem(i8 noundef %a, i8 noundef %b) {
  %1 = urem i8 %a, %b
  ret i8 %1
}
