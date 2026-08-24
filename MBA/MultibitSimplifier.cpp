// MSiMBA — Multi-bit (semi-linear) MBA simplifier.
// Port of external/MSiMBA/Mba.Common/MSiMBA/MultibitSiMBA.cs.
#include "MultibitSimplifier.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <functional>
#include <unordered_set>

#include "ConstantSubstituter.h"
#include "GeneralSimplifier.h"
#include "LinearSimplifier.h"
#include "MultibitRefiner.h"
#include "Parser.h"
#include "Verify.h"

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
    : bitCount(bitCount), modRed(modRed),
      moduloMask(bitCount >= 64 ? ~0ull : ((1ull << bitCount) - 1)),
      ast(nullptr), variables(), varCount(0), numCombinations(0) {
  ast = parse(expr, bitCount, modRed, false, false);
  if (!ast)
    return;

  hasBitwiseOps_ = hasBitwiseOps();

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

// ================================================================ has bitwise ops

bool MultibitSimplifier::hasBitwiseOps() const {
  std::function<bool(const std::shared_ptr<Node> &)> check =
      [&](const std::shared_ptr<Node> &node) -> bool {
    if (node->type == NodeType::CONJUNCTION ||
        node->type == NodeType::INCL_DISJUNCTION ||
        node->type == NodeType::EXCL_DISJUNCTION ||
        node->type == NodeType::NEGATION)
      return true;
    for (auto &child : node->children)
      if (check(child))
        return true;
    return false;
  };
  return check(ast);
}

bool MultibitSimplifier::hasXorWithConstant() const {
  std::function<bool(const std::shared_ptr<Node> &)> check =
      [&](const std::shared_ptr<Node> &node) -> bool {
    if (node->type == NodeType::EXCL_DISJUNCTION) {
      // Check if any child is a constant.
      for (auto &child : node->children)
        if (child->type == NodeType::CONSTANT)
          return true;
    }
    for (auto &child : node->children)
      if (check(child))
        return true;
    return false;
  };
  return check(ast);
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
  // The constant offset is updated by XOR recovery (matching the C# reference).
  MultibitRefiner refiner(bitCount, moduloMask);
  std::vector<std::shared_ptr<Node>> terms;
  uint64_t constantOffset = constant;

  for (size_t i = 0; i < linearCombinations.size(); i++) {
    auto &entries = linearCombinations[i];
    if (entries.empty())
      continue;

    // Use the refiner to simplify the linear combination.
    auto coeffToMask = refiner.simplifyEntry(entries);

    // Try to recover an XOR (updates constantOffset).
    auto *xorResult = refiner.trySimplifyXor(constantOffset, coeffToMask);
    if (xorResult) {
      constantOffset = xorResult->adjustedConstant;
      // Normalize: if coefficient is negative, flip sign, complement XOR
      // constant, and adjust constant offset.
      // -b*(x^~C) + k  =>  b*(x^C) + (k+b)
      int64_t signedCoeff = static_cast<int64_t>(xorResult->coeff);
      if (signedCoeff < 0) {
        uint64_t posCoeff = static_cast<uint64_t>(-signedCoeff);
        xorResult->coeff = posCoeff;
        xorResult->xorMask = moduloMask & ~xorResult->xorMask;
        constantOffset = (constantOffset + posCoeff) & moduloMask;
      }
      // Normalize: clear the top bit of the XOR constant.
      // For odd b: b*(x^(C^2^63)) + (k+2^63) == b*(x^C) + k (mod 2^64)
      // For even b: b*(x^(C^2^63)) + k == b*(x^C) + k (mod 2^64)
      uint64_t topBit = 1ull << (bitCount - 1);
      if (xorResult->xorMask & topBit) {
        xorResult->xorMask &= ~topBit;
        if (xorResult->coeff & 1) // odd coefficient
          constantOffset = (constantOffset + topBit) & moduloMask;
      }
      // XOR term: coeff * (xorMask ^ conj)
      auto conj = conjunctionFromVarMask(variableCombinations[i]);
      if (conj) {
        auto xorNode = ast->newNode(NodeType::EXCL_DISJUNCTION);
        auto maskNode = ast->newConstantNode(static_cast<int64_t>(xorResult->xorMask));
        // GT rule: if 'x' present → x^C (var first), else C^y (const first).
        bool hasX = std::find(variables.begin(), variables.end(), "x") != variables.end();
        if (hasX) {
          xorNode->children.push_back(conj);
          xorNode->children.push_back(maskNode);
        } else {
          xorNode->children.push_back(maskNode);
          xorNode->children.push_back(conj);
        }
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

    // Try to isolate a single variable.
    // Skip isolation if the expression contains XOR (the isolation
    // produces incorrect results for XOR expressions because the
    // coeffToMask doesn't have the XOR-with-1 pair).
    std::optional<uint64_t> varCoeffOpt;
    if (!hasBitwiseOps_) {
      varCoeffOpt = refiner.tryIsolateVariable(coeffToMask);
    }
    if (varCoeffOpt.has_value()) {
      uint64_t varCoeff = *varCoeffOpt;
      if (varCoeff != 0) {
        auto conj = conjunctionFromVarMask(variableCombinations[i]);
        if (conj) {
          if (varCoeff == 1) {
            terms.push_back(conj);
          } else {
            auto mulNode = ast->newNode(NodeType::PRODUCT);
            auto constNode = ast->newConstantNode(static_cast<int64_t>(varCoeff));
            mulNode->children.push_back(constNode);
            mulNode->children.push_back(conj);
            terms.push_back(mulNode);
          }
        }
      }
      auto *xorResult2 = refiner.trySimplifyXor(constantOffset, coeffToMask);
      if (xorResult2) {
        constantOffset = xorResult2->adjustedConstant;
        int64_t sc2 = static_cast<int64_t>(xorResult2->coeff);
        if (sc2 < 0) {
          uint64_t pc2 = static_cast<uint64_t>(-sc2);
          xorResult2->coeff = pc2;
          xorResult2->xorMask = moduloMask & ~xorResult2->xorMask;
          constantOffset = (constantOffset + pc2) & moduloMask;
        }
        auto conj2 = conjunctionFromVarMask(variableCombinations[i]);
        if (conj2) {
          auto xorNode = ast->newNode(NodeType::EXCL_DISJUNCTION);
          auto maskNode = ast->newConstantNode(static_cast<int64_t>(xorResult2->xorMask));
          xorNode->children.push_back(conj2);
          xorNode->children.push_back(maskNode);
          if (xorResult2->coeff != 1) {
            auto mulNode = ast->newNode(NodeType::PRODUCT);
            auto constNode = ast->newConstantNode(static_cast<int64_t>(xorResult2->coeff));
            mulNode->children.push_back(constNode);
            mulNode->children.push_back(xorNode);
            terms.push_back(mulNode);
          } else {
            terms.push_back(xorNode);
          }
        }
      }
      continue;
    }

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

  // Try to collapse b*x + b*y - 2*b*(x&y) into b*(x^y).
  bool xor3Found = false;
  for (size_t i = 0; i < terms.size() && !xor3Found; i++) {
    auto &ti = terms[i];
    if (ti->type != NodeType::PRODUCT || ti->children.size() != 2)
      continue;
    if (ti->children[0]->type != NodeType::CONSTANT)
      continue;
    uint64_t coeffI = static_cast<uint64_t>(ti->children[0]->constant.getSExtValue());
    if (coeffI == 0)
      continue;
    for (size_t j = i + 1; j < terms.size() && !xor3Found; j++) {
      auto &tj = terms[j];
      if (tj->type != NodeType::PRODUCT || tj->children.size() != 2)
        continue;
      if (tj->children[0]->type != NodeType::CONSTANT)
        continue;
      uint64_t coeffJ = static_cast<uint64_t>(tj->children[0]->constant.getSExtValue());
      if (coeffJ != coeffI)
        continue;
      uint64_t neg2Coeff = moduloMask & (moduloMask * (2 * coeffI));
      for (size_t k = 0; k < terms.size() && !xor3Found; k++) {
        if (k == i || k == j)
          continue;
        auto &tk = terms[k];
        if (tk->type != NodeType::PRODUCT || tk->children.size() != 2)
          continue;
        if (tk->children[0]->type != NodeType::CONSTANT)
          continue;
        uint64_t coeffK = static_cast<uint64_t>(tk->children[0]->constant.getSExtValue());
        if (coeffK != neg2Coeff)
          continue;
        // Check varK == varI & varJ.
        auto &varI = ti->children[1];
        auto &varJ = tj->children[1];
        auto &varK = tk->children[1];
        bool isAnd = false;
        if (varK->type == NodeType::CONJUNCTION && varK->children.size() == 2) {
          auto &kc0 = varK->children[0];
          auto &kc1 = varK->children[1];
          auto match = [](const std::shared_ptr<Node> &a,
                          const std::shared_ptr<Node> &b) {
            return a->type == b->type && a->constant == b->constant;
          };
          if ((match(kc0, varI) && match(kc1, varJ)) ||
              (match(kc0, varJ) && match(kc1, varI)))
            isAnd = true;
        }
        if (!isAnd)
          continue;

        // Build b*(x^y).
        auto xorNode = ast->newNode(NodeType::EXCL_DISJUNCTION);
        xorNode->children.push_back(varI->getCopy());
        xorNode->children.push_back(varJ->getCopy());
        std::shared_ptr<Node> xorTerm;
        if (coeffI == 1) {
          xorTerm = xorNode;
        } else {
          auto mulNode = ast->newNode(NodeType::PRODUCT);
          mulNode->children.push_back(
              ast->newConstantNode(static_cast<int64_t>(coeffI)));
          mulNode->children.push_back(xorNode);
          xorTerm = mulNode;
        }

        // Collect indices to remove, sorted descending.
        std::vector<size_t> removeIdx = {i, j, k};
        std::sort(removeIdx.begin(), removeIdx.end(), std::greater<size_t>());
        for (auto idx : removeIdx)
          terms.erase(terms.begin() + idx);
        size_t insertPos = std::min({i, j, k});
        if (insertPos > terms.size())
          insertPos = terms.size();
        terms.insert(terms.begin() + insertPos, xorTerm);
        xor3Found = true;
      }
    }
  }

  // Add the (possibly updated) constant offset.
  // GT rule: if any term is a PRODUCT (coeff != 1), constant comes first.
  // If all terms are simple variables (coeff == 1), variable comes first
  // when 'x' is present, constant first when only 'y'.
  if (constantOffset != 0) {
    auto constNode = ast->newConstantNode(static_cast<int64_t>(constantOffset));
    bool hasProduct = false;
    for (auto &t : terms) {
      if (t->type == NodeType::PRODUCT) {
        hasProduct = true;
        break;
      }
    }
    if (getenv("MSIMBA_DEBUG")) {
      fprintf(stderr, "  [dbg] hasProduct=%d vars=", hasProduct);
      for (auto &v : variables) fprintf(stderr, "%s ", v.c_str());
      fprintf(stderr, " terms(%zu)=", terms.size());
      for (auto &t : terms) fprintf(stderr, "%s(%d) ", t->toString().c_str(), (int)t->type);
      fprintf(stderr, "\n");
    }
    if (hasProduct) {
      terms.insert(terms.begin(), constNode); // constant first
    } else {
      // Check if the expression contains the "primary" variable.
      // Primary = 'x' for single-letter vars, 'v0' for vN vars.
      bool hasPrimary =
          std::find(variables.begin(), variables.end(), "x") != variables.end() ||
          std::find(variables.begin(), variables.end(), "v0") != variables.end();
      if (hasPrimary) {
        terms.push_back(constNode); // variable first
      } else {
        terms.insert(terms.begin(), constNode); // constant first
      }
    }
  }

  if (terms.empty())
    return "0";

  // Sort terms: variables alphabetically, then products, constants stay
  // in their inserted position (first or last).
  // Simple approach: sort non-constant terms by string representation.
  auto isConst = [](const std::shared_ptr<Node> &n) {
    return n->type == NodeType::CONSTANT;
  };
  // Separate constants from non-constants.
  std::vector<std::shared_ptr<Node>> nonConst;
  std::shared_ptr<Node> constTerm;
  bool constWasFirst = false;
  for (auto &t : terms) {
    if (isConst(t)) {
      if (!constTerm) {
        constTerm = t;
        constWasFirst = (t == terms.front());
      }
    } else {
      nonConst.push_back(t);
    }
  }
  // Sort non-constant terms by string (alphabetical for single vars).
  std::sort(nonConst.begin(), nonConst.end(),
            [](const std::shared_ptr<Node> &a,
               const std::shared_ptr<Node> &b) {
              return a->toString() < b->toString();
            });
  // Reassemble: constant first or last, non-constants in sorted order.
  terms.clear();
  if (constTerm && constWasFirst)
    terms.push_back(constTerm);
  for (auto &t : nonConst)
    terms.push_back(t);
  if (constTerm && !constWasFirst)
    terms.push_back(constTerm);

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

// ================================================================ constant substitution

bool MultibitSimplifier::containsXor(const std::shared_ptr<Node> &node) {
  if (node->type == NodeType::EXCL_DISJUNCTION)
    return true;
  for (auto &child : node->children)
    if (containsXor(child))
      return true;
  return false;
}

std::shared_ptr<Node> MultibitSimplifier::rewriteXorToAnd(
    const std::shared_ptr<Node> &node, uint64_t moduloMask) {
  // Recursively rewrite children first.
  bool changed = false;
  for (auto &child : node->children) {
    auto rewritten = rewriteXorToAnd(child, moduloMask);
    if (rewritten != child) {
      child = rewritten;
      changed = true;
    }
  }

  // If this node is XOR with a constant, rewrite it.
  // x^C = x + C - 2*(x&C)
  if (node->type == NodeType::EXCL_DISJUNCTION && node->children.size() == 2) {
    std::shared_ptr<Node> varChild, constChild;
    for (auto &child : node->children) {
      if (child->type == NodeType::CONSTANT)
        constChild = child;
      else
        varChild = child;
    }
    if (constChild && varChild) {
      uint64_t C = constChild->constant.getZExtValue() & moduloMask;
      int bw = node->bitCount;

      // Build: x + C + (-2) * (x&C)
      // Term 1: x
      auto term1 = varChild->getCopy();

      // Term 2: C (constant)
      auto term2 = std::make_shared<Node>(NodeType::CONSTANT, bw);
      term2->constant = MBAOps::fromSigned(static_cast<int64_t>(C));

      // Term 3: -2 * (x&C)
      auto andNode = std::make_shared<Node>(NodeType::CONJUNCTION, bw);
      andNode->children.push_back(varChild->getCopy());
      andNode->children.push_back(constChild->getCopy());
      auto neg2 = std::make_shared<Node>(NodeType::CONSTANT, bw);
      uint64_t neg2val = (moduloMask + 1 - 2) & moduloMask; // -2 mod 2^N
      neg2->constant = MBAOps::fromSigned(static_cast<int64_t>(neg2val));
      auto term3 = std::make_shared<Node>(NodeType::PRODUCT, bw);
      term3->children.push_back(neg2);
      term3->children.push_back(andNode);

      // Sum: x + C + (-2)*(x&C)
      auto sum = std::make_shared<Node>(NodeType::SUM, bw);
      sum->children.push_back(term1);
      sum->children.push_back(term2);
      sum->children.push_back(term3);
      return sum;
    }
  }

  return node;
}

std::string MultibitSimplifier::simplifyViaConstantSubstitution(
    const std::shared_ptr<Node> &ast) const {
  // If the solution contains XOR, skip the 1-bit shortcut.
  // The 1-bit SiMBA cannot handle XOR of variables (created by substituting
  // constants in XOR terms).
  if (containsXor(ast))
    return "";

  // Collect existing variable names.
  std::unordered_set<std::string> existingVars;
  std::function<void(const std::shared_ptr<Node> &)> collectVars =
      [&](const std::shared_ptr<Node> &node) {
        if (node->type == NodeType::VARIABLE)
          existingVars.insert(node->vname);
        for (auto &child : node->children)
          collectVars(child);
      };
  collectVars(ast);

  // Apply constant substitution.
  auto [substituted, substMapping] =
      ConstantSubstituter::apply(ast, existingVars);
  if (!substituted || substMapping.empty())
    return ""; // No constants to substitute.

  // Convert the substituted AST to a string.
  std::string substExpr = substituted->toString();

  // Run the 1-bit SiMBA solver on the substituted expression.
  std::string simplifiedStr = simplifyLinearMba(substExpr, bitCount, false, false,
                                                 modRed, true, -1,
                                                 Metric::ALTERNATION);
  if (simplifiedStr.empty())
    return "";

  // Parse the simplified expression back to an AST.
  Parser parser(simplifiedStr, bitCount, modRed);
  auto simplified = parser.parseExpression();
  if (!simplified)
    return simplifiedStr; // Return the string if parsing fails.

  // Back-substitute the constants.
  auto result = ConstantSubstituter::applyBackSubstitution(simplified, substMapping);
  if (!result)
    return simplifiedStr;

  return result->toString();
}

// ================================================================ normalization

std::shared_ptr<Node> MultibitSimplifier::normalizeNegatedSum(
    const std::shared_ptr<Node> &node, uint64_t moduloMask) {
  // Pattern: a + b*x where a == -b (mod 2^N)  =>  b*~x
  // The SUM must have exactly 2 children: one constant, one product (const * var).
  if (node->type != NodeType::SUM || node->children.size() != 2)
    return node;

  // Identify which child is the constant and which is the product.
  std::shared_ptr<Node> constChild, prodChild;
  for (auto &child : node->children) {
    if (child->type == NodeType::CONSTANT)
      constChild = child;
    else if (child->type == NodeType::PRODUCT && child->children.size() == 2)
      prodChild = child;
  }
  if (!constChild || !prodChild)
    return node;

  // The product must be: constant * variable (or variable * constant).
  std::shared_ptr<Node> prodConst, prodVar;
  for (auto &child : prodChild->children) {
    if (child->type == NodeType::CONSTANT)
      prodConst = child;
    else if (child->type == NodeType::VARIABLE)
      prodVar = child;
  }
  if (!prodConst || !prodVar)
    return node;

  // Check: constChild == prodConst (mod 2^N), i.e. -c + (-c*x) pattern.
  // In this case, -c + (-c*x) = c*~x.
  uint64_t a = constChild->constant.getZExtValue() & moduloMask;
  uint64_t b = prodConst->constant.getZExtValue() & moduloMask;
  if (a != b)
    return node;
  // The result coefficient is -b (mod 2^N), since -c + (-c*x) = c*~x
  // where c = -b.
  uint64_t c = (moduloMask + 1 - b) & moduloMask; // c = -b mod 2^N

  // Build: c * ~x  (where c = -b mod 2^N)
  auto negNode = std::make_shared<Node>(NodeType::NEGATION, node->bitCount);
  negNode->children.push_back(prodVar);
  auto cNode = std::make_shared<Node>(NodeType::CONSTANT, node->bitCount);
  cNode->constant = MBAOps::fromSigned(static_cast<int64_t>(c));
  auto result = std::make_shared<Node>(NodeType::PRODUCT, node->bitCount);
  result->children.push_back(cNode);
  result->children.push_back(negNode);
  return result;
}

// ================================================================ main entry

std::string MultibitSimplifier::simplify(const std::string &expr, int bitCount,
                                         bool modRed) {
  MultibitSimplifier solver(expr, bitCount, modRed);
  if (!solver.ast || solver.varCount == 0 || solver.varCount > 15)
    return "";

  solver.buildResultVector();

  // Check if the expression is actually linear (uniform result vector).
  // Only delegate to the 1-bit SiMBA path if the expression has no bitwise
  // operations (AND, OR, XOR, NOT). Bitwise ops with constants make the
  // expression semi-linear, which the 1-bit path cannot handle correctly.
  bool isLinear = solver.isLinearResultVector();
  bool hasBitwise = solver.hasBitwiseOps();
  if (isLinear && !hasBitwise) {
    // Delegate to the 1-bit SiMBA path.
    std::string result = simplifyLinearMba(expr, bitCount, false, false,
                                            modRed, true, -1,
                                            Metric::ALTERNATION);
    if (!result.empty())
      return result;
    return expr;
  }

  // Multi-bit path: find the initial linear combination.
  std::string result = solver.simplifyGeneric();

  // Try the constant substitution + 1-bit SiMBA shortcut on the SOLUTION.
  // Parse the solution back to an AST, substitute constants, run 1-bit SiMBA,
  // then back-substitute.
  if (!result.empty()) {
    Parser solParser(result, bitCount, modRed);
    auto solAst = solParser.parseExpression();
    if (solAst) {
      std::string csResult = solver.simplifyViaConstantSubstitution(solAst);
      if (!csResult.empty() && csResult.size() < result.size())
        result = csResult;
    }
  }

  // Verification gate: fast-check the result against the input.
  // If the result is not equivalent, reject it.
  if (!result.empty() && result != expr) {
    if (!fastCheckEquivalent(expr, result, bitCount, 100, true))
      return "";
  }

  // Normalization: convert a + b*x (where a == -b) to b*~x.
  if (!result.empty()) {
    Parser normParser(result, bitCount, modRed);
    auto normAst = normParser.parseExpression();
    if (normAst) {
      uint64_t mask = (bitCount >= 64) ? ~0ULL : ((1ULL << bitCount) - 1);
      auto normalized = normalizeNegatedSum(normAst, mask);
      if (normalized != normAst) {
        std::string normStr = normalized->toString();
        if (fastCheckEquivalent(expr, normStr, bitCount, 100, true))
          result = normStr;
      }
    }
  }

  return result;
}

} // namespace MBA
} // namespace LSiMBA
