// MSiMBA — Multi-bit (semi-linear) MBA simplifier.
// Port of external/MSiMBA/Mba.Common/MSiMBA/MultibitSiMBA.cs.
//
// Handles semi-linear MBAs: expressions that would be linear if not for
// nontrivial constants inside bitwise operands (e.g. (x&5) + (y&3)).
//
// Algorithm:
//   1. Build a multi-bit signature vector (N-bit to N-bit transform).
//   2. Check if the expression is actually linear (uniform vector) → 1-bit path.
//   3. Find an initial linear combination of conjunctions.
//   4. Refine (merge terms, recover XORs, reduce coefficients).
//   5. Try 1-bit shortcut (constant substitution + SiMBA).
//   6. Verify with fast-check.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Node.h"
#include "Parser.h"

namespace LSiMBA {
namespace MBA {

class MultibitSimplifier {
public:
  // Main entry point: simplify a semi-linear MBA expression.
  // Returns the simplified expression string, or "" on failure.
  static std::string simplify(const std::string &expr, int bitCount,
                              bool modRed = false);

  // Check if an expression is semi-linear (has constants inside bitwise ops).
  static bool isSemiLinear(const std::string &expr);

  // Check if an expression is semi-linear by inspecting the AST.
  static bool isSemiLinearAST(const std::shared_ptr<Node> &ast);

private:
  int bitCount;
  uint64_t moduloMask;
  std::shared_ptr<Node> ast;
  std::vector<std::string> variables;
  int varCount;
  uint64_t numCombinations; // 2^varCount

  // Multi-bit result vector: size = numCombinations * bitCount.
  // Layout: [bit0_comb0, bit0_comb1, ..., bit0_comb(2^t-1),
  //          bit1_comb0, bit1_comb1, ..., bit1_comb(2^t-1), ...]
  std::vector<uint64_t> resultVector;

  MultibitSimplifier(const std::string &expr, int bitCount, bool modRed);

  // Build the multi-bit signature vector.
  void buildResultVector();

  // Check if the result vector corresponds to a linear expression.
  bool isLinearResultVector() const;

  // Check if the AST contains bitwise operations (AND, OR, XOR, NOT).
  bool hasBitwiseOps() const;

  // Find an initial linear combination of conjunctions.
  std::string simplifyGeneric();

  // Try the constant substitution + 1-bit SiMBA shortcut.
  std::string simplifyViaConstantSubstitution(const std::shared_ptr<Node> &ast) const;

  // Get group sizes for variable combinations.
  static std::vector<int> getGroupSizes(int varCount);

  // Get all variable combinations (bitmasks).
  static std::vector<uint64_t> getVariableCombinations(int varCount);

  // Get the group size index for a variable mask.
  static uint32_t getGroupSizeIndex(const std::vector<int> &groupSizes,
                                    uint64_t varMask);

  bool modRed;

  // Subtract a coefficient from the result vector.
  void subtractCoeff(uint64_t coeff, int firstStart, int width,
                     bool onlyOneVar, uint64_t trueMask, int bitIndex);

  // Build a conjunction (AND of variables) from a variable mask.
  std::shared_ptr<Node> conjunctionFromVarMask(uint64_t varMask) const;

  // Build a term: coeff * (mask & conjunction).
  std::shared_ptr<Node> term(const std::shared_ptr<Node> &conj,
                             uint64_t coeff, uint64_t mask) const;

  // Cost metric: string length.
  static int cost(const std::string &expr);
};

} // namespace MBA
} // namespace LSiMBA
