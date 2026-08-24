// MSiMBA — Multi-bit (semi-linear) MBA simplifier.
// Port of external/MSiMBA/Mba.Common/MSiMBA/MultibitSiMBA.cs.
#include "MultibitSimplifier.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <functional>
#include <unordered_set>

#include "LinearSimplifier.h"
#include "MultibitRefiner.h"

namespace LSiMBA {
namespace MBA {

// ================================================================ helpers

std::vector<int> MultibitSimplifier::getGroupSizes(int varCount) {
  std::vector<int> sizes(1, 1);
  for (int i = 0; i < varCount; i++)
    sizes.push_back(2 * sizes.back());
  return sizes;
}

std::vector<uint64_t> MultibitSimplifier::getVariableCombinations(int varCount) {
  // Returns all non-empty subsets of {0..varCount-1} as bitmasks,
  // ordered by increasing popcount then by value.
  // Total number of non-empty subsets = 2^varCount - 1.
  int numEntries = (1 << varCount) - 1;
  std::vector<uint64_t> outputs(numEntries, 0);
  for (int i = 0; i < varCount; i++)
    outputs[i] = 1ull << i;

  int combCount = varCount;
  int newCount = varCount;
  for (int count = 1; count < varCount; count++) {
    int size = combCount;
    int nnew = 0;
    int from = size - newCount;
    for (int ei = from; ei < size; ei++) {
      uint64_t e = outputs[ei];
      int lastIdx = 63 - __builtin_clzll(e);
      for (int v = lastIdx + 1; v < varCount; v++) {
        if (combCount < numEntries)
          outputs[combCount] = (e | (1ull << v));
        combCount++;
        nnew++;
      }
    }
    newCount = nnew;
  }
  return outputs;
}

uint32_t MultibitSimplifier::getGroupSizeIndex(const std::vector<int> &groupSizes,
                                               uint64_t varMask) {
  uint32_t sum = 0;
  while (varMask != 0) {
    int lsb = __builtin_ctzll(varMask);
    sum += static_cast<uint32_t>(groupSizes[lsb]);
    varMask ^= (1ull << lsb);
  }
  return sum;
}

int MultibitSimplifier::cost(const std::string &expr) {
  return static_cast<int>(expr.size());
}

// ================================================================ semi-linear check

bool MultibitSimplifier::isSemiLinearAST(const std::shared_ptr<Node> &ast) {
  // A semi-linear expression has a constant inside a bitwise operand.
  // Walk the AST: if we find a CONSTANT that is a child of a bitwise op
  // (CONJUNCTION, EXCL_DISJUNCTION, INCL_DISJUNCTION, NEGATION-in-bitwise),
  // and the constant is not 0 or -1 (all-ones), it's semi-linear.
  std::function<bool(const std::shared_ptr<Node> &, bool)> walk =
      [&](const std::shared_ptr<Node> &n, bool inBitwise) -> bool {
    if (!n)
      return false;
    switch (n->type) {
    case NodeType::CONJUNCTION:
    case NodeType::EXCL_DISJUNCTION:
    case NodeType::INCL_DISJUNCTION:
      for (auto &child : n->children) {
        if (child->type == NodeType::CONSTANT) {
          uint64_t v = MBAOps::toLow64(child->constant, child->bitCount);
          uint64_t mask = (child->bitCount >= 64)
                              ? ~0ull
                              : ((1ull << child->bitCount) - 1);
          // Nontrivial constant: not 0, not all-ones.
          if (v != 0 && v != mask)
            return true;
        } else if (walk(child, true)) {
          return true;
        }
      }
      return false;
    case NodeType::NEGATION:
      if (inBitwise) {
        auto &c = n->children[0];
        if (c->type == NodeType::CONSTANT) {
          uint64_t v = MBAOps::toLow64(c->constant, c->bitCount);
          uint64_t mask = (c->bitCount >= 64)
                              ? ~0ull
                              : ((1ull << c->bitCount) - 1);
          if (v != 0 && v != mask)
            return true;
        } else if (walk(c, inBitwise)) {
          return true;
        }
      }
      return false;
    case NodeType::CONSTANT:
    case NodeType::VARIABLE:
      return false;
    default:
      for (auto &child : n->children)
        if (walk(child, false))
          return true;
      return false;
    }
  };
  return walk(ast, false);
}

bool MultibitSimplifier::isSemiLinear(const std::string &expr) {
  auto ast = parse(expr, 64, false, false, false);
  if (!ast)
    return false;
  return isSemiLinearAST(ast);
}

// ================================================================ constructor

MultibitSimplifier::MultibitSimplifier(const std::string &expr, int bitCount,
                                       bool modRed)
    : bitCount(bitCount),
      moduloMask(bitCount >= 64 ? ~0ull : ((1ull << bitCount) - 1)),
      ast(nullptr), variables(), varCount(0), numCombinations(0) {
  ast = parse(expr, bitCount, modRed, false, false);
  if (!ast)
    return;

  ast->collectVariables(variables);
  ast->enumerateVariables(variables);
  varCount = static_cast<int>(variables.size());
  if (varCount == 0 || varCount > 15)
    return; // too many variables
  numCombinations = 1ull << varCount;
}

// ================================================================ result vector

void MultibitSimplifier::buildResultVector() {
  // Multi-bit signature vector: size = numCombinations * bitCount.
  // For each bit index i, evaluate the expression with each variable set to
  // either 0 or (1 << i), then shift the result right by i.
  resultVector.resize(numCombinations * static_cast<size_t>(bitCount), 0);

  for (uint32_t bitIndex = 0; bitIndex < static_cast<uint32_t>(bitCount); bitIndex++) {
    for (uint64_t comb = 0; comb < numCombinations; comb++) {
      // Set each variable's value.
      std::vector<uint64_t> values(varCount, 0);
      for (int v = 0; v < varCount; v++) {
        uint64_t varMask = 1ull << v;
        uint64_t varValue = (comb & varMask) >> v; // 0 or 1
        values[v] = varValue << bitIndex;          // shift to current bit
      }

      // Evaluate the AST.
      uint64_t eval = ast->eval(values);
      eval = moduloMask & eval;

      // Shift down by bitIndex (the N-bit to 1-bit transform).
      eval >>= bitIndex;

      resultVector[bitIndex * numCombinations + comb] = eval;
    }
  }
}

// ================================================================ linearity check

bool MultibitSimplifier::isLinearResultVector() const {
  // Check if the multi-bit result vector is uniform across all bit indices
  // (after shifting the constant offset). If so, the expression is actually
  // linear and can be simplified with the 1-bit SiMBA path.
  if (resultVector.empty())
    return false;

  uint64_t constant = resultVector[0];

  // Build a 1-bit linear expression from row 0.
  std::shared_ptr<Node> linearExpr;
  for (uint64_t i = 0; i < numCombinations; i++) {
    uint64_t coeff = moduloMask & (resultVector[i] - constant);
    if (coeff == 0)
      continue;

    // Build the conjunction for this combination.
    std::shared_ptr<Node> conj;
    for (int v = 0; v < varCount; v++) {
      uint64_t varMask = 1ull << v;
      bool isSet = (i & varMask) != 0;
      auto varNode = ast->newVariableNode(variables[v]);
      if (!isSet) {
        auto neg = ast->newNode(NodeType::NEGATION);
        neg->children.push_back(varNode);
        varNode = neg;
      }
      if (!conj)
        conj = varNode;
      else {
        auto andNode = ast->newNode(NodeType::CONJUNCTION);
        andNode->children.push_back(conj);
        andNode->children.push_back(varNode);
        conj = andNode;
      }
    }

    // Multiply by coefficient.
    auto mulNode = ast->newNode(NodeType::PRODUCT);
    auto constNode = ast->newConstantNode(static_cast<int64_t>(coeff));
    mulNode->children.push_back(constNode);
    mulNode->children.push_back(conj);

    if (!linearExpr)
      linearExpr = mulNode;
    else {
      auto addNode = ast->newNode(NodeType::SUM);
      addNode->children.push_back(linearExpr);
      addNode->children.push_back(mulNode);
      linearExpr = addNode;
    }
  }

  if (!linearExpr)
    linearExpr = ast->newConstantNode(0);

  // Set vidx for the new AST's variables so eval() works.
  linearExpr->enumerateVariables(variables);

  // Build the multi-bit vector for the linear expression.
  std::vector<uint64_t> otherVec(numCombinations * static_cast<size_t>(bitCount), 0);
  for (uint32_t bitIndex = 0; bitIndex < static_cast<uint32_t>(bitCount); bitIndex++) {
    for (uint64_t comb = 0; comb < numCombinations; comb++) {
      std::vector<uint64_t> values(varCount, 0);
      for (int v = 0; v < varCount; v++) {
        uint64_t varMask = 1ull << v;
        uint64_t varValue = (comb & varMask) >> v;
        values[v] = varValue << bitIndex;
      }
      uint64_t eval = linearExpr->eval(values);
      eval = moduloMask & eval;
      eval >>= bitIndex;
      otherVec[bitIndex * numCombinations + comb] = eval;
    }
  }

  // Compare the two vectors (after shifting constant offset per row).
  for (uint32_t bitIndex = 0; bitIndex < static_cast<uint32_t>(bitCount); bitIndex++) {
    uint64_t constantOffset = moduloMask & (constant >> bitIndex);
    for (uint64_t i = 0; i < numCombinations; i++) {
      uint64_t v0 = moduloMask & (resultVector[bitIndex * numCombinations + i] -
                                  constantOffset);
      uint64_t v1 = moduloMask & otherVec[bitIndex * numCombinations + i];
      if (v0 != v1)
        return false;
    }
  }
  return true;
}

// ================================================================ subtract coeff

void MultibitSimplifier::subtractCoeff(uint64_t coeff, int firstStart, int width,
                                       bool onlyOneVar, uint64_t trueMask,
                                       int bitIndex) {
  int offset = bitIndex * width;
  int v0 = __builtin_ctzll(trueMask);
  int groupSize1 = 1 << v0;
  int period1 = 2 * groupSize1;
  for (int start = firstStart; start < width; start += period1) {
    for (int i = start; i < start + groupSize1; i++) {
      uint64_t castI = static_cast<uint64_t>(static_cast<uint32_t>(i));
      bool isTrue2 = (castI & trueMask) == trueMask;
      if (i != firstStart && (onlyOneVar || isTrue2))
        resultVector[offset + i] =
            moduloMask & (resultVector[offset + i] - coeff);
    }
  }
}

// ================================================================ conjunction builder

std::shared_ptr<Node>
MultibitSimplifier::conjunctionFromVarMask(uint64_t varMask) const {
  std::shared_ptr<Node> conj;
  for (int v = 0; v < varCount; v++) {
    uint64_t bit = 1ull << v;
    if ((varMask & bit) == 0)
      continue;
    auto varNode = ast->newVariableNode(variables[v]);
    if (!conj)
      conj = varNode;
    else {
      auto andNode = ast->newNode(NodeType::CONJUNCTION);
      andNode->children.push_back(conj);
      andNode->children.push_back(varNode);
      conj = andNode;
    }
  }
  return conj;
}

std::shared_ptr<Node>
MultibitSimplifier::term(const std::shared_ptr<Node> &conj, uint64_t coeff,
                         uint64_t mask) const {
  // Build: coeff * (mask & conj)
  // If mask is all-ones, just: coeff * conj
  // If coeff is 1, just: mask & conj
  uint64_t fullMask = (bitCount >= 64) ? ~0ull : ((1ull << bitCount) - 1);

  std::shared_ptr<Node> inner;
  if (mask == fullMask) {
    inner = conj;
  } else {
    auto andNode = ast->newNode(NodeType::CONJUNCTION);
    auto maskNode = ast->newConstantNode(static_cast<int64_t>(mask));
    andNode->children.push_back(maskNode);
    andNode->children.push_back(conj);
    inner = andNode;
  }

  if (coeff == 1)
    return inner;

  auto mulNode = ast->newNode(NodeType::PRODUCT);
  auto constNode = ast->newConstantNode(static_cast<int64_t>(coeff));
  mulNode->children.push_back(constNode);
  mulNode->children.push_back(inner);
  return mulNode;
}

// ================================================================ simplify generic

std::string MultibitSimplifier::simplifyGeneric() {
  // Subtract the constant offset from each row (shifted by bit index).
  uint64_t constant = resultVector[0];
  for (uint32_t bitIndex = 0; bitIndex < static_cast<uint32_t>(bitCount); bitIndex++) {
    uint64_t constantOffset = moduloMask & (constant >> bitIndex);
    for (uint64_t i = 0; i < numCombinations; i++) {
      size_t idx = bitIndex * numCombinations + i;
      resultVector[idx] = moduloMask & (resultVector[idx] - constantOffset);
    }
  }

  // Get all variable combinations.
  auto variableCombinations = getVariableCombinations(varCount);
  auto groupSizes = getGroupSizes(varCount);

  // For each combination, compute the group size index.
  std::vector<std::pair<uint64_t, int>> combToMaskAndIdx;
  for (size_t i = 0; i < variableCombinations.size(); i++) {
    uint64_t myMask = variableCombinations[i];
    int myIndex = static_cast<int>(getGroupSizeIndex(groupSizes, myMask));
    combToMaskAndIdx.push_back({myMask, myIndex});
  }

  bool onlyOneVar = (varCount == 1);
  int width = (varCount == 1) ? 1 : (2 << (varCount - 1));

  // Linear combination: for each basis expression, a list of (coeff, bitMask).
  std::vector<std::vector<std::pair<uint64_t, uint64_t>>> linearCombinations(
      variableCombinations.size());

  for (uint32_t bitIndex = 0; bitIndex < static_cast<uint32_t>(bitCount); bitIndex++) {
    uint64_t maskForIndex = (1ull << bitIndex);
    int offset = static_cast<int>(bitIndex * numCombinations);

    for (size_t i = 0; i < variableCombinations.size(); i++) {
      uint64_t comb = variableCombinations[i];
      auto &pair = combToMaskAndIdx[i];
      uint64_t trueMask = pair.first;
      int index = pair.second;

      uint64_t coeff = resultVector[offset + index];
      if (coeff == 0)
        continue;

      subtractCoeff(coeff, index, width, onlyOneVar, trueMask,
                    static_cast<int>(bitIndex));

      linearCombinations[i].push_back({coeff, maskForIndex});
    }
  }

  // Build the expression from the linear combinations, using the refiner.
  MultibitRefiner refiner(bitCount, moduloMask);
  std::vector<std::shared_ptr<Node>> terms;
  if (constant != 0) {
    terms.push_back(ast->newConstantNode(static_cast<int64_t>(constant)));
  }

  for (size_t i = 0; i < linearCombinations.size(); i++) {
    auto &entries = linearCombinations[i];
    if (entries.empty())
      continue;

    // Use the refiner to simplify the linear combination.
    auto coeffToMask = refiner.simplifyEntry(entries);

    // Try to recover an XOR.
    auto *xorResult = refiner.trySimplifyXor(constant, coeffToMask);
    if (xorResult) {
      // XOR term: coeff * (xorMask ^ conj)
      auto conj = conjunctionFromVarMask(variableCombinations[i]);
      if (conj) {
        auto xorNode = ast->newNode(NodeType::EXCL_DISJUNCTION);
        auto maskNode = ast->newConstantNode(static_cast<int64_t>(xorResult->xorMask));
        xorNode->children.push_back(maskNode);
        xorNode->children.push_back(conj);
        if (xorResult->coeff != 1) {
          auto mulNode = ast->newNode(NodeType::PRODUCT);
          auto constNode = ast->newConstantNode(static_cast<int64_t>(xorResult->coeff));
          mulNode->children.push_back(constNode);
          mulNode->children.push_back(xorNode);
          terms.push_back(mulNode);
        } else {
          terms.push_back(xorNode);
        }
      }
      continue;
    }

    // Try to isolate a single variable (disabled for now — produces
    // incorrect results in the multi-bit case).
    // uint64_t varCoeff = refiner.tryIsolateVariable(constant, coeffToMask);
    // if (varCoeff != 0) {
    //   auto conj = conjunctionFromVarMask(variableCombinations[i]);
    //   if (conj) {
    //     if (varCoeff == 1) {
    //       terms.push_back(conj);
    //     } else {
    //       auto mulNode = ast->newNode(NodeType::PRODUCT);
    //       auto constNode = ast->newConstantNode(static_cast<int64_t>(varCoeff));
    //       mulNode->children.push_back(constNode);
    //       mulNode->children.push_back(conj);
    //       terms.push_back(mulNode);
    //     }
    //   }
    //   continue;
    // }

    // Build terms for each (coeff, mask) pair.
    auto conj = conjunctionFromVarMask(variableCombinations[i]);
    if (!conj)
      continue;

    for (auto &[coeff, mask] : coeffToMask) {
      if (coeff == 0 || mask == 0)
        continue;
      terms.push_back(term(conj, coeff, mask));
    }
  }

  if (terms.empty())
    return "0";

  // Combine all terms with SUM.
  std::shared_ptr<Node> result = terms[0];
  for (size_t i = 1; i < terms.size(); i++) {
    auto addNode = ast->newNode(NodeType::SUM);
    addNode->children.push_back(result);
    addNode->children.push_back(terms[i]);
    result = addNode;
  }

  return result->toString();
}

// ================================================================ main entry

std::string MultibitSimplifier::simplify(const std::string &expr, int bitCount,
                                         bool modRed) {
  MultibitSimplifier solver(expr, bitCount, modRed);
  if (!solver.ast || solver.varCount == 0 || solver.varCount > 15)
    return "";

  solver.buildResultVector();

  // Check if the expression is actually linear.
  if (solver.isLinearResultVector()) {
    // Delegate to the 1-bit SiMBA path.
    std::string result = simplifyLinearMba(expr, bitCount, false, false,
                                            modRed, true, -1,
                                            Metric::ALTERNATION);
    if (!result.empty())
      return result;
    return expr;
  }

  // Multi-bit path: find the initial linear combination.
  return solver.simplifyGeneric();
}

} // namespace MBA
} // namespace LSiMBA
