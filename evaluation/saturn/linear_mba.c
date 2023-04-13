#include <stdint.h>

int64_t mba0(int64_t a, int64_t b) { return ((a ^ b) + 2 * (a & b)); }

uint64_t mba1(uint64_t x, uint64_t y, uint64_t z) {
  return ((~z & (~((~y & z) + y) & z) + (~y & z) + y) +
          (x & (x ^ y) + (x & y) * 0x2) * 0x2 + (~((~x | z) + x + 0x1) & z) +
          (x ^ (x ^ y) + (x & y) * 0x2) + (y & (~y & z) + y) + (~x | z) + x +
          z + 0x1 - ((~y & z) + y | (~((~y & z) + y) & z) + (~y & z) + y)) +
         ((~z & (~((~y & z) + y) & z) + (~y & z) + y) +
          (x & (x ^ y) + (x & y) * 0x2) * 0x2 + (~((~x | z) + x + 0x1) & z) +
          (x ^ (x ^ y) + (x & y) * 0x2) + (y & (~y & z) + y) + (~x | z) + x +
          z + 0x1 - ((~y & z) + y | (~((~y & z) + y) & z) + (~y & z) + y));
}

uint32_t mba2(uint32_t x, uint32_t y, uint32_t z) {
  return 3 * (x | ~y | z) - 4 * (~x | ~z) + (x ^ ~z) - 7 * (x & (y ^ ~z)) +
         3 * (x ^ ~y & z) - 2 * (x & ~y & z) + 5 * (~x | ~y & ~z) - 2 * (~x) -
         2 * (x & y ^ z) - 2;
}

int main(int argc, char **argv) {
  return mba0(argc, argc) + mba1(argc, argc, argc) + mba2(argc, argc, argc);
}

void __fastcall vm_xor(struct stack *a1) {
  __int64 v1;         // rax
  unsigned __int8 v2; // cl
  __int64 v3;         // rsi

  v1 = *a1;
  v2 = v1 - 1;
  v3 = *(a1 + v2 + 1) - *(a1 + v1 + 1) -
       2LL * (*(a1 + v2 + 1) | ~*(a1 + v1 + 1));
  *a1 = v1 - 1;
  *(a1 + v2 + 1) = 5889 * v3 - 7426;
}

void vm_xor(struct stack *a1) {
  __int64 v1; // r8
  __int64 v2; // rdx
  __int64 v3; // rax

  v1 = *a1;
  v2 = (v1 - 1);
  v3 = 5889 * (*(a1 + v1 + 1) ^ *(a1 + v2 + 1)) + 4352;
  *a1 = v2;
  *(a1 + v2 + 1) = v3;
}