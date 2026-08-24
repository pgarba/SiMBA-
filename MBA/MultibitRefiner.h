// MSiMBA — Multi-bit refiner for semi-linear MBA expressions.
// Port of external/MSiMBA/Mba.Common/MSiMBA/MultibitRefiner.cs.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Node.h"

namespace LSiMBA {
namespace MBA {

class MultibitRefiner {
public:
  MultibitRefiner(int bitSize, uint64_t moduloMask)
      : bitSize(bitSize), moduloMask(moduloMask) {}

  // Core checks.
  bool canChangeCoefficientTo(uint64_t oldCoeff, uint64_t newCoeff,
                              uint64_t mask) const;
  bool canChangeMaskTo(uint64_t coeff, uint64_t oldMask,
                       uint64_t newMask) const;
  bool canRemoveMask(uint64_t coeff, uint64_t mask) const;

  // Simplify a single basis expression's linear combination.
  // Input: list of (coeff, bitMask) pairs.
  // Output: map of coeff → merged mask.
  std::unordered_map<uint64_t, uint64_t>
  simplifyEntry(const std::vector<std::pair<uint64_t, uint64_t>> &entries) const;

  // Try to recover an XOR from inverse-coefficient pairs.
  // Returns (adjustedConstant, coeff, xorMask) or nullptr.
  struct XorResult {
    uint64_t adjustedConstant;
    uint64_t coeff;
    uint64_t xorMask;
  };
  XorResult *trySimplifyXor(uint64_t constantOffset,
                            std::unordered_map<uint64_t, uint64_t> &coeffToMask) const;

  // Try to isolate a single variable conjunction.
  // Returns the coefficient if the basis expression is just a variable,
  // or 0 if not.
  uint64_t tryIsolateVariable(uint64_t constantOffset,
                              std::unordered_map<uint64_t, uint64_t> &coeffToMask) const;

  // Try to express m1*(a&c1) + m2*(a&c2) as (m1-m2)*(a&c1) + m2*a.
  uint64_t tryExpressAsSingleBitwiseSum(
      std::unordered_map<uint64_t, uint64_t> &coeffToMask) const;

  // Try to remove a negated double sum: m*(a&c) + 2m*(a&~c) → m*a + m*(a&c).
  uint64_t tryRemoveNegatedDoubleSum(
      uint64_t coeff, std::unordered_map<uint64_t, uint64_t> &coeffToMask) const;

  // Try to express 3 terms as 2 (coefficient sum).
  void tryEliminateUniqueValues(
      std::unordered_map<uint64_t, uint64_t> &coeffToMask) const;

private:
  int bitSize;
  uint64_t moduloMask;
};

} // namespace MBA
} // namespace LSiMBA
