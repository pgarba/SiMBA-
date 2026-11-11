#ifndef PIECEWISE_H
#define PIECEWISE_H

#include <cstdint>
#include <functional>
#include <string>

namespace LSiMBA {

/*
  Single-variable expressions that are a simple base - a, ~a, -a or nothing -
  combined (^ or +) with a residual that takes one value up to a threshold
  and another above it:

    g(a) ^ (a <=u T ? C1 : C0)

  Obfuscators hide the comparison as a borrow, e.g. ((K-1) - (a & ~1)) >> 1,
  and no MBA simplifier can return it: their results have no comparisons.

  F evaluates the candidate at a Width-bit value (8 <= Width <= 64) of its
  variable. Returns the replacement over the variable V in SiMBA's string
  syntax, with the comparisons "<=u" / "<=s" (0 or 1), or "" when the
  expression has another shape. The result is a guess from samples: callers
  must prove it.
*/
std::string simplifyPiecewise(const std::function<uint64_t(uint64_t)> &F,
                              int Width, const std::string &V);

}  // namespace LSiMBA

#endif
