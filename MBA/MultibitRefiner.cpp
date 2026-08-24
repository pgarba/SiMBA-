// MSiMBA — Multi-bit refiner implementation.
#include "MultibitRefiner.h"

#include <algorithm>
#include <climits>

namespace LSiMBA {
namespace MBA {

// ================================================================ core checks

bool MultibitRefiner::canChangeCoefficientTo(uint64_t oldCoeff, uint64_t newCoeff,
                                             uint64_t mask) const {
  // Port of C# reference's CanChangeCoeff: check 4-bit patterns.
  uint64_t iter = mask;
  while (iter) {
    int pos = __builtin_ctzll(iter);
    iter &= ~(0xFull << pos);
    uint64_t value = (0xFull << pos) & mask; // 4-bit window at pos
    uint64_t diff = (oldCoeff - newCoeff) * value;
    // ReduceOr: check if any bit of diff is set in moduloMask.
    uint64_t nope = diff & moduloMask;
    // ReduceOr: OR all bits together.
    nope |= nope >> 1;
    nope |= nope >> 2;
    nope |= nope >> 4;
    nope |= nope >> 8;
    nope |= nope >> 16;
    nope |= nope >> 32;
    if (nope & 1)
      return false;
  }
  return true;
}

bool MultibitRefiner::canChangeMaskTo(uint64_t coeff, uint64_t oldMask,
                                      uint64_t newMask) const {
  for (int i = 0; i < bitSize; i++) {
    uint64_t value = 1ull << i;
    uint64_t op1 = moduloMask & (coeff * (value & oldMask));
    uint64_t op2 = moduloMask & (coeff * (value & newMask));
    if (op1 != op2)
      return false;
  }
  return true;
}

bool MultibitRefiner::canRemoveMask(uint64_t coeff, uint64_t mask) const {
  return canChangeMaskTo(coeff, mask, moduloMask);
}

// ================================================================ simplify entry

std::unordered_map<uint64_t, uint64_t>
MultibitRefiner::simplifyEntry(const std::vector<std::pair<uint64_t, uint64_t>> &entries) const {
  // (1) Merge expressions with the same coefficient (OR their masks).
  std::unordered_map<uint64_t, uint64_t> coeffToMask;
  for (auto &[coeff, mask] : entries) {
    if (coeff == 0)
      continue;
    coeffToMask[coeff] |= mask;
  }

  // (2) Try to merge terms with different coefficients by changing one.
  // Port of C# reference's SimplifyDisjointSumMultiply.
  auto reduceTermCount = [this](std::unordered_map<uint64_t, uint64_t> &c2m) {
    std::vector<std::pair<uint64_t, uint64_t>> arr;
    for (auto &[c, m] : c2m)
      if (m != 0)
        arr.push_back({c, m});
    std::sort(arr.begin(), arr.end(),
              [](auto &a, auto &b) { return a.first > b.first; });
    for (size_t a = 0; a < arr.size(); a++) {
      if (arr[a].second == 0)
        continue;
      for (size_t b = a + 1; b < arr.size(); b++) {
        if (arr[a].second == 0)
          break;
        if (arr[b].second == 0)
          continue;
        if ((arr[a].second & arr[b].second) != 0)
          continue;
        if (canChangeCoefficientTo(arr[b].first, arr[a].first, arr[b].second)) {
          arr[a].second |= arr[b].second;
          arr[b].second = 0;
        } else if (canChangeCoefficientTo(arr[a].first, arr[b].first, arr[a].second)) {
          arr[b].second |= arr[a].second;
          arr[a].second = 0;
          break;
        }
      }
    }
    c2m.clear();
    for (auto &[c, m] : arr)
      if (m != 0)
        c2m[c] |= m;
  };

  reduceTermCount(coeffToMask);

  // (3) Discard terms with coefficient → 0.
  for (auto it = coeffToMask.begin(); it != coeffToMask.end();) {
    if (canChangeCoefficientTo(it->first, 0, it->second))
      it = coeffToMask.erase(it);
    else
      ++it;
  }

  // (3b) Try to reduce the number of terms again (matching C# reference).
  reduceTermCount(coeffToMask);

  // (4) Reduce coefficients to -1 where possible.
  uint64_t negOne = moduloMask; // -1 mod 2^N
  std::vector<uint64_t> toReduce;
  for (auto &[c, m] : coeffToMask) {
    if (c != negOne && canChangeCoefficientTo(c, negOne, m))
      toReduce.push_back(c);
  }
  for (auto c : toReduce) {
    coeffToMask[negOne] |= coeffToMask[c];
    coeffToMask.erase(c);
  }

  // (5) Try to express 3 terms as 2.
  tryEliminateUniqueValues(coeffToMask);

  return coeffToMask;
}

// ================================================================ XOR recovery

MultibitRefiner::XorResult *
MultibitRefiner::trySimplifyXor(uint64_t constantOffset,
                                std::unordered_map<uint64_t, uint64_t> &coeffToMask) const {
  // Look for a pair (coeffA, coeffB) where coeffB == -coeffA.
  // Iterate in descending order (matching C# reference heuristic).
  std::vector<uint64_t> keys;
  for (auto &[c, m] : coeffToMask)
    if (c != 0)
      keys.push_back(c);
  std::sort(keys.begin(), keys.end(), std::greater<uint64_t>());

  for (auto coeffA : keys) {
    uint64_t maskA = coeffToMask[coeffA];
    uint64_t inverse = moduloMask & (moduloMask * coeffA); // -coeffA
    if (inverse == coeffA)
      continue;
    auto it = coeffToMask.find(inverse);
    if (it == coeffToMask.end())
      continue;
    uint64_t maskB = it->second;

    // Check if maskB can be changed to ~maskA.
    uint64_t negatedMask = moduloMask & ~maskA;
    if (!canChangeMaskTo(inverse, maskB, negatedMask))
      continue;

    // Found an XOR.
    uint64_t multiplied = moduloMask & (inverse * maskA);
    uint64_t adjustedConstant = moduloMask & (constantOffset - multiplied);

    // Remove both terms.
    coeffToMask.erase(coeffA);
    coeffToMask.erase(inverse);

    // Return the XOR result.
    static XorResult result;
    result.adjustedConstant = adjustedConstant;
    result.coeff = inverse;
    result.xorMask = maskA;
    return &result;
  }
  return nullptr;
}

// ================================================================ isolate variable

uint64_t MultibitRefiner::tryIsolateVariable(
    uint64_t constantOffset,
    std::unordered_map<uint64_t, uint64_t> &coeffToMask) const {
  // If there's only one entry with mask == all-ones, it's just a variable.
  if (coeffToMask.size() == 1) {
    auto &[coeff, mask] = *coeffToMask.begin();
    if (mask == moduloMask || canRemoveMask(coeff, mask)) {
      coeffToMask.clear();
      return coeff;
    }
    return 0;
  }

  uint64_t variableCoefficient = 0;

  // Try to remove negated double sums.
  std::vector<uint64_t> keys;
  for (auto &[c, m] : coeffToMask)
    if (c != 0 && m != 0)
      keys.push_back(c);

  for (auto coeff : keys) {
    auto it = coeffToMask.find(coeff);
    if (it == coeffToMask.end())
      continue;
    uint64_t mask = it->second;
    if (mask == 0)
      continue;

    // Try to remove a negated double sum.
    uint64_t sumCoeff = tryRemoveNegatedDoubleSum(coeff, coeffToMask);
    if (sumCoeff != 0) {
      variableCoefficient = moduloMask & (variableCoefficient + sumCoeff);
      continue;
    }

    // Check if we can remove the bitmask.
    if (canRemoveMask(coeff, mask)) {
      coeffToMask[coeff] = 0;
      variableCoefficient = moduloMask & (variableCoefficient + coeff);
    }
  }

  // Try to express as single bitwise sum.
  uint64_t result = tryExpressAsSingleBitwiseSum(coeffToMask);
  variableCoefficient = moduloMask & (variableCoefficient + result);

  return variableCoefficient;
}

// ================================================================ single bitwise sum

uint64_t MultibitRefiner::tryExpressAsSingleBitwiseSum(
    std::unordered_map<uint64_t, uint64_t> &coeffToMask) const {
  uint64_t result = 0;
  std::vector<uint64_t> keys;
  for (auto &[c, m] : coeffToMask)
    if (c != 0 && m != 0)
      keys.push_back(c);

  for (size_t a = 0; a < keys.size(); a++) {
    for (size_t b = 0; b < keys.size(); b++) {
      if (a == b)
        continue;
      uint64_t coeffA = keys[a], coeffB = keys[b];
      auto itA = coeffToMask.find(coeffA);
      auto itB = coeffToMask.find(coeffB);
      if (itA == coeffToMask.end() || itB == coeffToMask.end())
        continue;
      uint64_t maskA = itA->second, maskB = itB->second;
      if (maskA == 0 || maskB == 0)
        continue;

      // Try: m1*(a&c1) + m2*(a&c2) → (m1-m2)*(a&c1) + m2*a
      uint64_t sum1 = moduloMask & (coeffA - coeffB);
      // Check if we can rewrite.
      bool canRewrite = true;
      for (int i = 0; i < bitSize; i++) {
        uint64_t value = 1ull << i;
        uint64_t op1 = moduloMask & (coeffA * (value & maskA) + coeffB * (value & maskB));
        uint64_t op2 = moduloMask & (sum1 * (value & maskA) + coeffB * value);
        if (op1 != op2) {
          canRewrite = false;
          break;
        }
      }
      if (!canRewrite)
        continue;

      // Remove both old terms.
      coeffToMask[coeffA] = 0;
      coeffToMask[coeffB] = 0;
      // Add new term.
      coeffToMask[sum1] = maskA;
      result = moduloMask & (result + coeffB);
      // No break — C# reference continues to find more matches.
    }
  }
  return result;
}

// ================================================================ negated double sum

uint64_t MultibitRefiner::tryRemoveNegatedDoubleSum(
    uint64_t coeff, std::unordered_map<uint64_t, uint64_t> &coeffToMask) const {
  uint64_t doubleCoeff = moduloMask & (2 * coeff);
  auto it = coeffToMask.find(doubleCoeff);
  if (it == coeffToMask.end())
    return 0;
  uint64_t otherMask = it->second;
  uint64_t thisMask = coeffToMask[coeff];
  if ((moduloMask & ~thisMask) != otherMask)
    return 0;

  // Found a match.
  coeffToMask[doubleCoeff] = 0;
  coeffToMask[coeff] = otherMask;
  return coeff;
}

// ================================================================ 3→2 terms

void MultibitRefiner::tryEliminateUniqueValues(
    std::unordered_map<uint64_t, uint64_t> &coeffToMask) const {
  std::vector<std::pair<uint64_t, uint64_t>> values;
  for (auto &[c, m] : coeffToMask)
    if (c != 0)
      values.push_back({c, m});

  int l = static_cast<int>(values.size());
  if (l < 3)
    return;

  for (int i = 0; i < l - 1; i++) {
    for (int j = i + 1; j < l; j++) {
      for (int k = 0; k < l; k++) {
        if (k == i || k == j)
          continue;
        auto &[ca, ma] = values[i];
        auto &[cb, mb] = values[j];
        auto &[cc, mc] = values[k];
        if (ca == 0 || cb == 0 || cc == 0)
          continue;

        // Check: ca + cb == cc, and all masks are disjoint.
        if ((moduloMask & (ca + cb)) != cc)
          continue;
        if ((ma & mb) != 0 || (ma & mc) != 0 || (mb & mc) != 0)
          continue;

        // Express 3 terms as 2.
        values[i] = {ca, ma | mc};
        values[j] = {cb, mb | mc};
        values[k] = {0, mc};
      }
    }
  }

  coeffToMask.clear();
  for (auto &[c, m] : values) {
    if (c == 0)
      continue;
    coeffToMask[c] |= m;
  }
}

} // namespace MBA
} // namespace LSiMBA
