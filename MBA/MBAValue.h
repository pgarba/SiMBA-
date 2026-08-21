// GAMBA native C++ port — modular value helpers.
//
// All MBA values live in the ring Z / 2^bitCount. Supported bitCount is 1..64
// (matching the project's Modulo.cpp).
//
// The Node "constant" field must hold:
//   * raw parsed constants, which for 64-bit MBAs reach 2^64-1, and
//   * negated constants (e.g. -1),
// which do not fit in int64_t. MSVC has no __int128, so we use the project's
// established llvm::APInt (128-bit, header-only) as the signed value type.
// Evaluated values / result vectors fit in uint64_t and stay uint64_t.
//
// Mirrors external/GAMBA/src/utils/node.py helpers (mod_red, popcount,
// trailing_zeros, power) and Node.__get_reduced_constant_closer_to_zero.
#ifndef MBA_MBAVALUE_H
#define MBA_MBAVALUE_H

#include <cstdint>
#include <string>

#include <llvm/ADT/APInt.h>
#include <llvm/ADT/SmallString.h>

namespace LSiMBA {
namespace MBA {

// 128-bit signed value used for the Node constant field.
using MBAValue = llvm::APInt;

struct MBAOps {
  // Sign-extend a signed 64-bit value to 128 bits.
  static MBAValue fromSigned(std::int64_t v) {
    std::uint64_t parts[2] = {static_cast<std::uint64_t>(v),
                              (v < 0) ? ~0ULL : 0ULL};
    return MBAValue(128, 2, parts);
  }

  // Signed decimal string (matches Python str() of the stored value).
  static std::string toStringSigned(const MBAValue &v) {
    llvm::SmallString<40> ss;
    v.toString(ss, 10, /*Signed=*/true);
    return std::string(ss.begin(), ss.end());
  }

  // The value mod 2^bitCount, as a uint64 in [0, 2^bitCount).
  static std::uint64_t toLow64(const MBAValue &v, int bitCount) {
    return v.zextOrTrunc(bitCount >= 64 ? 64 : bitCount).getZExtValue();
  }

  // Reduce v to [0, 2^bitCount). Identity for bitCount >= 64.
  static std::uint64_t reduce(std::uint64_t v, int bitCount) {
    if (bitCount >= 64)
      return v;
    return v & ((1ULL << bitCount) - 1ULL);
  }

  // Reduce v to the signed value with minimal absolute value in
  // [-2^(bitCount-1), 2^(bitCount-1)). Mirrors
  // Node.__get_reduced_constant_closer_to_zero.
  static std::int64_t reduceSigned(std::int64_t v, int bitCount) {
    std::uint64_t r = reduce(static_cast<std::uint64_t>(v), bitCount);
    if (bitCount >= 64)
      return static_cast<std::int64_t>(r); // int64 interpretation
    std::uint64_t half = 1ULL << (bitCount - 1);
    std::uint64_t full = 1ULL << bitCount;
    if (r > half)
      return static_cast<std::int64_t>(r - full);
    return static_cast<std::int64_t>(r);
  }

  // Number of 1-bits.
  static int popcount(std::uint64_t x) {
    int c = 0;
    while (x) {
      x &= x - 1;
      ++c;
    }
    return c;
  }

  // Number of trailing 0-bits. Returns 64 for x == 0.
  static int trailingZeros(std::uint64_t x) {
    if (x == 0)
      return 64;
    int n = 0;
    while ((x & 1ULL) == 0) {
      x >>= 1;
      ++n;
    }
    return n;
  }

  // b^e mod 2^bitCount. Mirrors node.py power(x, e, modulus).
  static std::uint64_t power(std::uint64_t b, std::uint64_t e, int bitCount) {
    if (b == 1)
      return 1;
    std::uint64_t r = 1;
    for (std::uint64_t i = 0; i < e; ++i) {
      r = reduce(r * b, bitCount);
      if (r == 0)
        return 0;
    }
    return r;
  }

  // All-ones mask for the width.
  static std::uint64_t widthMask(int bitCount) {
    if (bitCount >= 64)
      return ~0ULL;
    return (1ULL << bitCount) - 1ULL;
  }
};

} // namespace MBA
} // namespace LSiMBA

#endif // MBA_MBAVALUE_H
