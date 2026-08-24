// MSiMBA — Multi-bit refiner for semi-linear MBA expressions.
// Port of external/MSiMBA/Mba.Common/MSiMBA/MultibitRefiner.cs.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
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

  // Try to isolate a single variable conjunction (C# TryIsolateSingleVariableConjunction).
  // Returns the coefficient if successful, nullopt if not.
  std::optional<uint64_t> tryIsolateVariable(
      std::unordered_map<uint64_t, uint64_t> &coeffToMask) const;

  // Try to express m1*(a&c1) + m2*(a&c2) as (m1-m2)*(a&c1) + m2*a.
  uint64_t tryExpressAsSingleBitwiseSum(
      const std::vector<uint64_t> &keys,
      std::unordered_map<uint64_t, uint64_t> &coeffToMask) const;

  // Check if we can rewrite two terms with new coefficients and masks.
  bool canChangeSumMaskAndCoefficients(
      uint64_t oldCoeffA, uint64_t oldCoeffB, uint64_t oldMaskA, uint64_t oldMaskB,
      uint64_t newCoeffA, uint64_t newCoeffB, uint64_t newMaskA, uint64_t newMaskB) const;

  // Try to remove a negated double sum: m*(a&c) + 2m*(a&~c) → m*a.
  std::optional<uint64_t> tryRemoveNegatedDoubleSum(
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
