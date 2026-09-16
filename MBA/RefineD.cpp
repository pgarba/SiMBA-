// GAMBA native C++ port — Refine batch D.
// Mirrors external/GAMBA/src/utils/node.py (bitwise-in-sums rules,
// post-substitution refinement, polish, verification).
#include "Node.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>

namespace LSiMBA {
namespace MBA {

// Bounds to control performance (mirrors node.py).
static const int MAX_CHILDREN_SUMMED_UP = 3;
static const int MAX_CHILDREN_TO_TRANSFORM_BITW = 5;

// The non-negative residue of n modulo 2^bitCount (Python mod_red semantics).
static int64_t modRedValue(int64_t n, int bitCount) {
  if (bitCount >= 64)
    return n;
  return static_cast<int64_t>(static_cast<uint64_t>(n) & MBAOps::widthMask(bitCount));
}

// The stored constant as a non-negative int64 residue in [0, 2^bitCount).
static int64_t constLow(const Node &n, const MBAValue &c) {
  return static_cast<int64_t>(MBAOps::toLow64(c, n.bitCount));
}

// The stored constant as the signed value closest to zero.
static int64_t constClose(const Node &n, const MBAValue &c) {
  return MBAOps::reduceSigned(static_cast<int64_t>(MBAOps::toLow64(c, n.bitCount)), n.bitCount);
}

// The stored constant as used for factor arithmetic. Mirrors the Python stored
// constants: with modRed the non-negative residue, otherwise the signed value
// closest to zero.
static int64_t constFactor(const Node &n, const MBAValue &c) {
  return n.modRed ? constLow(n, c) : constClose(n, c);
}

// The children of the given node, except the first one (Python children[1:]).
static std::vector<std::shared_ptr<Node>> rest(const Node *n) {
  std::vector<std::shared_ptr<Node>> res;
  for (size_t i = 1; i < n->children.size(); ++i)
    res.push_back(n->children[i]);
  return res;
}

// ===================================================== substitution
void Node::replaceVariable(const std::string &vname, const std::shared_ptr<Node> &node) {
  if (type == NodeType::VARIABLE) {
    if (this->vname == vname)
      copyAll(*node);
    return;
  }
  for (auto &child : children)
    child->replaceVariable(vname, node);
}

// ===================================================== polish
void Node::polish(Node *parent) {
  // Iterate over a copy: a child's polish may call insertBitwiseNegations,
  // which multiplies the parent by a factor and inserts a constant into the
  // parent's children vector. Modifying the vector during the range-for would
  // invalidate the iterator (undefined behavior / hang).
  for (const auto &c : std::vector<std::shared_ptr<Node>>(children))
    c->polish(this);

  // Start (and end) with ordering for standardization in the steps in between.
  reorderVariables();
  resolveBitwiseNegationsInSums();
  insertBitwiseNegations(parent);
  reorderVariables();
}

void Node::resolveBitwiseNegationsInSums() {
  if (type != NodeType::SUM)
    return;

  // First rewrite all negations.
  int count = 0;
  for (size_t i = 0; i < children.size(); ++i) {
    if (children[i]->type != NodeType::NEGATION)
      continue;

    children[i] = children[i]->children[0];
    children[i]->multiplyByMinusOne();
    count += 1;
  }

  if (count != 0) {
    // Adapt the constant term.
    if (!children.empty() && children[0]->type == NodeType::CONSTANT) {
      children[0]->setAndReduceConstant(constLow(*this, children[0]->constant) - count);
      if (children[0]->isConstant(0))
        children.erase(children.begin());
    } else {
      children.insert(children.begin(), newConstantNode(-count));
    }
  }

  // If we have a constant term, check whether we can distribute it to terms
  // via negation.
  if (children.empty() || children[0]->type != NodeType::CONSTANT)
    return;

  // Non-negative residue of -constLow modulo 2^bitCount (uint64 so that the
  // 64-bit case yields the large value 2^64-1 instead of the signed -1, which
  // would otherwise let the early-return checks below pass and drop the constant).
  uint64_t negConst = MBAOps::reduce(
      static_cast<uint64_t>(-constLow(*this, children[0]->constant)), bitCount);
  if (static_cast<uint64_t>(children.size()) < negConst)
    return;

  int countM = countChildrenMultByMinusOne();
  // We cannot distribute the whole constant.
  if (static_cast<uint64_t>(countM) < negConst)
    return;

  int64_t todo = static_cast<int64_t>(negConst);
  // Insert bitwise negations.
  for (size_t i = 0; i < children.size(); ++i) {
    auto &child = children[i];

    // We are done:
    if (todo == 0)
      break;
    if (child->type != NodeType::PRODUCT)
      continue;
    if (child->children.empty() || !child->children[0]->isConstant(-1))
      continue;

    // Remove factor -1.
    child->children.erase(child->children.begin());

    if (child->children.size() == 1)
      child->type = NodeType::NEGATION;
    else
      children[i] = newNodeWithChildren(NodeType::NEGATION, {child->getShallowCopy()});
    todo -= 1;
  }

  // Finally remove the constant.
  if (!children.empty())
    children.erase(children.begin());
}

int Node::countChildrenMultByMinusOne() const {
  int count = 0;
  for (auto &child : children) {
    if (child->type != NodeType::PRODUCT)
      continue;
    if (!child->children.empty() && child->children[0]->isConstant(-1))
      count += 1;
  }
  return count;
}

void Node::insertBitwiseNegations(Node *parent) {
  auto pair = getOptTransformedNegatedWithFactor();
  auto child = pair.first;
  int64_t factor = pair.second;

  // This node is no transformed negation with a factor.
  if (child == nullptr)
    return;

  type = NodeType::NEGATION;
  children = {child};

  if (factor == 1)
    return;

  if (parent != nullptr && parent->type == NodeType::PRODUCT)
    parent->multiply(factor);
  else
    multiply(factor);
}

std::pair<std::shared_ptr<Node>, int64_t> Node::getOptTransformedNegatedWithFactor() {
  if (type != NodeType::SUM)
    return {nullptr, 0};

  // Should not happen, but be sure.
  if (children.size() < 2)
    return {nullptr, 0};

  // First check for constant term.
  if (children[0]->type != NodeType::CONSTANT)
    return {nullptr, 0};

  // The constant factor.
  int64_t factor = constLow(*this, children[0]->constant);

  // A node to store the result in case we have more terms.
  auto res = newNode(NodeType::SUM);

  // Now collect the remaining terms. Stop if we encounter a term without a
  // factor divisible by factor.
  for (size_t i = 1; i < children.size(); ++i) {
    res->children.push_back(children[i]->getCopy());
    auto &child = res->children.back();

    // If the factor is 1, nothing more to do.
    if (modRedValue(factor - 1, bitCount) == 0)
      continue;

    // If the factor is -1, multiply the child by -1.
    if (modRedValue(factor + 1, bitCount) == 0) {
      child->multiplyByMinusOne();
      continue;
    }

    // Otherwise we need a factor that is divisible by factor.
    if (child->type != NodeType::PRODUCT)
      return {nullptr, 0};
    if (child->children.empty() || child->children[0]->type != NodeType::CONSTANT)
      return {nullptr, 0};

    auto &constNode = child->children[0];
    int64_t c = constClose(*this, constNode->constant);

    // We cannot divide the constant factor by factor.
    if (factor == 0 || c % factor != 0)
      return {nullptr, 0};

    constNode->setAndReduceConstant(c / factor);
    if (constNode->isConstant(1)) {
      res->children.back()->children.erase(res->children.back()->children.begin());
      if (res->children.back()->children.size() == 1)
        res->children.back() = res->children.back()->children[0];
    }
  }

  if (res->children.size() == 1)
    return {res->children[0], -factor};
  return {res, -factor};
}

// ===================================================== verify via evaluation
bool Node::checkVerify(const std::shared_ptr<Node> &other, int bitCount) {
  std::vector<std::string> variables;
  other->collectAndEnumerateVariables(variables);
  enumerateVariables(variables);
  int vnumber = static_cast<int>(variables.size());

  uint64_t mask = MBAOps::widthMask(bitCount);
  uint64_t total = 1ULL << (vnumber * bitCount);

  for (uint64_t i = 0; i < total; ++i) {
    uint64_t n = i;
    std::vector<uint64_t> par;
    for (int j = 0; j < vnumber; ++j) {
      par.push_back(n & mask);
      n = bitCount >= 64 ? 0 : n >> bitCount;
    }

    uint64_t v1 = other->eval(par);
    uint64_t v2 = eval(par);

    if (v1 != v2) {
      printf("\n*** ... verification failed for input %llu: orig %llu, output %llu\n",
             (unsigned long long)i, (unsigned long long)v1, (unsigned long long)v2);
      return false;
    }
  }

  printf("*** ... verification successful!\n");
  return true;
}

// ===================================================== refine after substitution
namespace {
// FNV-1a 64-bit fold (offset basis passed in via the caller's h).
inline uint64_t fnv1a64(uint64_t h, uint64_t x) {
  h ^= x;
  h *= 1099511628211ULL;
  return h;
}
} // namespace

Node::PatternRefine Node::refineAfterSubstitutionDetail() {
  bool childStable = true;   // all children Unchanged
  bool childChanged = false; // some child Changed
  for (auto &c : children) {
    auto r = c->refineAfterSubstitutionDetail();
    if (r == PatternRefine::Changed)
      childChanged = true;
    else if (r == PatternRefine::Stable)
      childStable = false;
  }

  uint64_t h1 = 1469598103934665603ULL;
  uint64_t h2 = 1469598103934665604ULL;
  auto fold = [&h1, &h2](uint64_t x) {
    h1 = fnv1a64(h1, x);
    h2 = fnv1a64(h2, x ^ 0x9E3779B97F4A7C15ULL);
  };
  // Structural fingerprint (no object pointers): a deep copy preserves it,
  // so getCopy propagates the cached fingerprint and unchanged subtrees of a
  // candidate remain skippable.
  fold(static_cast<uint64_t>(type));
  fold(constant.getZExtValue());
  fold(constant.shl(64).getZExtValue());
  for (auto &c : children) {
    fold(static_cast<uint64_t>(c->type));
    fold(c->constant.getZExtValue());
    fold(c->constant.shl(64).getZExtValue());
  }

  if (childStable && patternHashValid && h1 == patternHash1 && h2 == patternHash2)
    return PatternRefine::Unchanged;

  patternHashValid = false;
  bool changed = childChanged;
  auto timeCheck = [this](int idx, auto fn) -> bool {
    if (checkPerf().enabled) {
      auto t0 = std::chrono::steady_clock::now();
      bool r = fn();
      checkPerf().t[idx] += std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - t0)
                                 .count();
      if (r)
        checkPerf().fired[idx]++;
      checkPerf().calls[idx]++;
      return r;
    }
    return fn();
  };
  if (timeCheck(0, [this] { return checkBitwiseInSumsCancelTerms(); }))
    changed = true;
  if (timeCheck(1, [this] { return checkBitwiseInSumsReplaceTerms(); }))
    changed = true;
  if (timeCheck(2, [this] { return checkDisjInvolvingXorInSums(); }))
    changed = true;
  if (timeCheck(3, [this] { return checkXorInvolvingDisj(); }))
    changed = true;
  if (timeCheck(4, [this] { return checkNegativeBitwInverse(); }))
    changed = true;
  if (timeCheck(5, [this] { return checkXorPairsWithConstants(); }))
    changed = true;
  if (timeCheck(6, [this] { return checkBitwPairsWithConstants(); }))
    changed = true;
  if (timeCheck(7, [this] { return checkDiffBitwPairsWithConstants(); }))
    changed = true;
  if (timeCheck(8, [this] { return checkBitwTuplesWithConstants(); }))
    changed = true;
  if (timeCheck(9, [this] { return checkBitwPairsWithInverses(); }))
    changed = true;
  if (timeCheck(10, [this] { return checkDiffBitwPairsWithInverses(); }))
    changed = true;
  if (timeCheck(11, [this] { return checkBitwAndOpInSum(); }))
    changed = true;
  if (timeCheck(12, [this] { return checkInsertXorInSum(); }))
    changed = true;

  if (!changed) {
    patternHash1 = h1;
    patternHash2 = h2;
    patternHashValid = true;
    return PatternRefine::Stable;
  }
  return PatternRefine::Changed;
}

bool Node::refineAfterSubstitution() {
  return refineAfterSubstitutionDetail() == PatternRefine::Changed;
}

// ===================================================== bitwise in sums: cancel terms
bool Node::checkBitwiseInSumsCancelTerms() {
  if (type != NodeType::SUM)
    return false;
  // Save runtime in too large sums.
  if (children.size() > MAX_CHILDREN_TO_TRANSFORM_BITW)
    return false;

  bool changed = false;  // W2: was `true` (reported changed even without a rewrite)
  int i = 0;
  while (true) {
    if (i >= static_cast<int>(children.size()))
      return changed;

    auto child = children[i];
    int64_t factor = 1;

    if (child->type == NodeType::PRODUCT) {
      if (child->children.size() != 2 || child->children[0]->type != NodeType::CONSTANT) {
        i += 1;
        continue;
      }

      factor = constFactor(*child, child->children[0]->constant);
      child = child->children[1];
    }

    if ((child->type != NodeType::CONJUNCTION && child->type != NodeType::INCL_DISJUNCTION) ||
        child->children.size() != 2) {
      i += 1;
      continue;
    }

    int newIdx = checkTransformBitwiseInSumCancel(i, child, factor);
    if (newIdx == -1) {
      i += 1;
      continue;
    }

    // If the sum has only one child left, replace it by this child.
    if (children.size() == 1) {
      copy(*children[0]);
      return true;
    }

    // Otherwise adapt the iteration index.
    changed = true;
    i = newIdx + 1;
  }
}

int Node::checkTransformBitwiseInSumCancel(int idx, const std::shared_ptr<Node> &bitw,
                                          int64_t factor) {
  // For a transformation to Xor, we need a factor divisible by 2.
  bool withToXor = (factor % 2) == 0;

  int newIdx = checkTransformBitwiseInSumCancelImpl(false, idx, bitw, factor);
  if (newIdx != -1)
    return newIdx;

  if (withToXor) {
    newIdx = checkTransformBitwiseInSumCancelImpl(true, idx, bitw, factor / 2);
    if (newIdx != -1)
      return newIdx;
  }

  return -1;
}

int Node::checkTransformBitwiseInSumCancelImpl(bool toXor, int idx,
                                              const std::shared_ptr<Node> &bitw,
                                              int64_t factor) {
  auto opSum = newNode(NodeType::SUM);
  for (auto &op : bitw->children)
    opSum->children.push_back(op->getCopy());
  opSum->multiply(factor);
  opSum->expand();
  opSum->refine();

  // Save runtime. It is not likely that we can sum up more children to a node
  // equal to opSum.
  int maxc = std::min(static_cast<int>(opSum->children.size()), MAX_CHILDREN_SUMMED_UP);

  // Iterate over all subsets of children and check whether the children sum up
  // to the sum of the bitwise operation's operands.
  for (int i = 1; i < (1 << (static_cast<int>(children.size()) - 1)); ++i) {
    // Save runtime, see above.
    if (MBAOps::popcount(static_cast<uint64_t>(i)) > maxc)
      continue;

    int newIdx = checkTransformBitwiseForComb(toXor, idx, bitw, factor, opSum, i);
    if (newIdx != -1)
      return newIdx;
  }

  return -1;
}

int Node::checkTransformBitwiseForComb(bool toXor, int idx,
                                       const std::shared_ptr<Node> &bitw, int64_t factor,
                                       const std::shared_ptr<Node> &opSum, int combIdx) {
  int n = combIdx;

  auto diff = opSum->getCopy();
  std::vector<int> indices;

  for (int j = 0; j < static_cast<int>(children.size()); ++j) {
    if (j == idx)
      continue;

    if ((n & 1) == 1) {
      indices.push_back(j);
      diff->add(children[j]);
    }
    n >>= 1;
  }

  diff->expand();
  diff->refine();

  // Check whether we would get a better opportunity when adding a
  // modulo-multiple of the bitwise. This is in order not to miss something
  // due to a vanishing factor 2.
  if (diff->type != NodeType::CONSTANT) {
    auto opSum2 = newNode(NodeType::SUM);
    for (auto &op : bitw->children)
      opSum2->children.push_back(op->getCopy());

    int64_t hmod = static_cast<int64_t>(1ULL << (bitCount - 1));
    opSum2->multiply(-hmod);

    auto diff2 = diff->getCopy();
    diff2->add(opSum2);
    diff2->expand();
    diff2->refine();

    if (diff2->type == NodeType::CONSTANT) {
      diff = diff2;
      factor += hmod;
    }
  }

  return checkTransformBitwiseForDiff(toXor, idx, bitw, factor, diff, indices);
}

int Node::checkTransformBitwiseForDiff(bool toXor, int idx,
                                       const std::shared_ptr<Node> &bitw, int64_t factor,
                                       const std::shared_ptr<Node> &diff,
                                       const std::vector<int> &indices) {
  int newIdx = checkTransformBitwiseForDiffFull(toXor, idx, bitw, factor, diff, indices);
  if (newIdx != -1)
    return newIdx;

  // If there is only one term considered, merging will not decrease the
  // number of terms.
  if (indices.size() > 1) {
    // If the operand sum and the children's sum do not cancel out, check
    // whether their difference can be merged into any of the children.
    newIdx = checkTransformBitwiseForDiffMerge(toXor, idx, bitw, factor, diff, indices);
    if (newIdx != -1)
      return newIdx;
  }

  return -1;
}

int Node::checkTransformBitwiseForDiffFull(bool toXor, int idx,
                                          const std::shared_ptr<Node> &bitw, int64_t factor,
                                          const std::shared_ptr<Node> &diff,
                                          const std::vector<int> &indices) {
  if (diff->type != NodeType::CONSTANT)
    return -1;

  // We found a match. Transform the bitwise expression.
  if (!toXor || bitw->type == NodeType::CONJUNCTION)
    factor = -factor;
  bitw->type = bitw->getTransformedBitwiseType(toXor);
  if (modRedValue(factor - 1, bitCount) != 0)
    bitw->multiply(factor);
  children[idx] = bitw;

  // Finally remove the children which we have merged into the bitwise
  // expression.
  for (int j = static_cast<int>(indices.size()) - 1; j >= 0; --j) {
    children.erase(children.begin() + indices[j]);
    if (indices[j] < idx)
      idx -= 1;
  }

  // If the difference is not exactly zero, but constant, add this constant.
  if (!diff->isConstant(0)) {
    if (!children.empty() && children[0]->type != NodeType::CONSTANT)
      idx += 1;
    addConstant(constLow(*diff, diff->constant));

    // Check whether the sum's constant is now zero.
    if (!children.empty() && children[0]->isConstant(0)) {
      children.erase(children.begin());
      idx -= 1;
    }
  }

  return idx;
}

int Node::checkTransformBitwiseForDiffMerge(bool toXor, int idx,
                                           const std::shared_ptr<Node> &bitw, int64_t factor,
                                           const std::shared_ptr<Node> &diff,
                                           const std::vector<int> &indices) {
  MBAValue constN;
  bool hasN = diff->getOptConstFactor(constN);
  for (int i : indices) {
    auto child = children[i];
    MBAValue constC;
    bool hasC = child->getOptConstFactor(constC);
    if (!diff->equalsNeglectingConstants(*child, hasN, hasC))
      continue;

    // We found a child matching the difference. Transform the bitwise
    // expression.
    if (!toXor || bitw->type == NodeType::CONJUNCTION)
      factor = -factor;
    bitw->type = bitw->getTransformedBitwiseType(toXor);
    if (modRedValue(factor - 1, bitCount) != 0)
      bitw->multiply(factor);
    children[idx] = bitw;

    // Adapt the child's factor.
    if (hasC) {
      if (hasN) {
        child->children[0]->constant = constN;
      } else {
        child->children.erase(child->children.begin());
        if (child->children.size() == 1)
          child->copy(*child->children[0]);
      }
    } else if (hasN) {
      child->multiply(constLow(*child, constN));
    }

    // Finally remove the children which we have merged into the bitwise
    // expression.
    for (int j = static_cast<int>(indices.size()) - 1; j >= 0; --j) {
      if (indices[j] != i) {
        children.erase(children.begin() + indices[j]);
        if (indices[j] < idx)
          idx -= 1;
      }
    }

    return idx;
  }

  return -1;
}

NodeType Node::getTransformedBitwiseType(bool toXor) const {
  if (toXor)
    return NodeType::EXCL_DISJUNCTION;
  if (type == NodeType::CONJUNCTION)
    return NodeType::INCL_DISJUNCTION;
  return NodeType::CONJUNCTION;
}

// ===================================================== bitwise in sums: replace terms
bool Node::checkBitwiseInSumsReplaceTerms() {
  if (type != NodeType::SUM)
    return false;
  // Save runtime in too large sums.
  if (children.size() > MAX_CHILDREN_TO_TRANSFORM_BITW)
    return false;

  bool changed = false;  // W2: was `true` (reported changed even without a rewrite)
  int i = 0;
  while (true) {
    if (i >= static_cast<int>(children.size()))
      return changed;

    auto child = children[i];
    int64_t factor = 1;

    if (child->type == NodeType::PRODUCT) {
      if (child->children.size() != 2 || child->children[0]->type != NodeType::CONSTANT) {
        i += 1;
        continue;
      }

      factor = constFactor(*child, child->children[0]->constant);
      child = child->children[1];
    }

    if ((child->type != NodeType::CONJUNCTION && child->type != NodeType::INCL_DISJUNCTION) ||
        child->children.size() != 2) {
      i += 1;
      continue;
    }

    int newIdx = checkTransformBitwiseInSumReplace(i, child, factor);
    if (newIdx == -1) {
      i += 1;
      continue;
    }

    // Adapt the iteration index.
    changed = true;
    i = newIdx + 1;
  }

  // Unreachable (also in the Python source).
  return false;
}

int Node::checkTransformBitwiseInSumReplace(int idx, const std::shared_ptr<Node> &bitw,
                                           int64_t factor) {
  // Only continue if one operand is more complex than the other one.
  // Here we measure complexity via linearity.
  int cIdx = bitw->getIndexOfMoreComplexOperand();
  if (cIdx == -1)
    return -1;

  // For a transformation to Xor, we need a factor divisible by 2.
  bool withToXor = (factor % 2) == 0;

  int newIdx = checkTransformBitwiseInSumReplaceImpl(false, idx, bitw, cIdx, factor);
  if (newIdx != -1)
    return newIdx;

  if (withToXor) {
    newIdx = checkTransformBitwiseInSumReplaceImpl(true, idx, bitw, cIdx, factor / 2);
    if (newIdx != -1)
      return newIdx;
  }

  return -1;
}

int Node::getIndexOfMoreComplexOperand() {
  children[0]->markLinear();
  children[1]->markLinear();

  bool l0 = children[0]->isLinear();
  bool l1 = children[1]->isLinear();

  if (l0 != l1)
    return l1 ? 0 : 1;

  bool b0 = children[0]->state == NodeState::BITWISE;
  bool b1 = children[1]->state == NodeState::BITWISE;

  if (b0 != b1)
    return b1 ? 0 : 1;
  return -1;
}

int Node::checkTransformBitwiseInSumReplaceImpl(bool toXor, int idx,
                                               const std::shared_ptr<Node> &bitw, int cIdx,
                                               int64_t factor) {
  auto cOp = bitw->children[cIdx]->getCopy();
  cOp->multiply(factor);

  // Save runtime. It is not likely that we can sum up more children to a node
  // equal to cOp.
  int maxc = MAX_CHILDREN_SUMMED_UP;

  // Iterate over all subsets of children and check whether the children sum up
  // to the bitwise operation's operand.
  for (int i = 1; i < (1 << (static_cast<int>(children.size()) - 1)); ++i) {
    // Save runtime, see above.
    if (MBAOps::popcount(static_cast<uint64_t>(i)) > maxc)
      continue;

    int newIdx = checkTransformBitwiseReplaceForComb(toXor, idx, bitw, factor, cOp, cIdx, i);
    if (newIdx != -1)
      return newIdx;
  }

  return -1;
}

int Node::checkTransformBitwiseReplaceForComb(bool toXor, int idx,
                                             const std::shared_ptr<Node> &bitw, int64_t factor,
                                             const std::shared_ptr<Node> &cOp, int cIdx,
                                             int combIdx) {
  int n = combIdx;

  auto diff = cOp->getCopy();
  std::vector<int> indices;

  for (int j = 0; j < static_cast<int>(children.size()); ++j) {
    if (j == idx)
      continue;

    if ((n & 1) == 1) {
      indices.push_back(j);
      diff->add(children[j]);
    }
    n >>= 1;
  }

  diff->expand();
  diff->refine();

  return checkTransformBitwiseReplaceForDiff(toXor, idx, bitw, factor, diff, cIdx, indices);
}

int Node::checkTransformBitwiseReplaceForDiff(bool toXor, int idx,
                                             const std::shared_ptr<Node> &bitw, int64_t factor,
                                             const std::shared_ptr<Node> &diff, int cIdx,
                                             const std::vector<int> &indices) {
  return checkTransformBitwiseReplaceForDiffFull(toXor, idx, bitw, factor, diff, cIdx, indices);
}

int Node::checkTransformBitwiseReplaceForDiffFull(bool toXor, int idx,
                                                 const std::shared_ptr<Node> &bitw, int64_t factor,
                                                 const std::shared_ptr<Node> &diff, int cIdx,
                                                 const std::vector<int> &indices) {
  if (diff->type != NodeType::CONSTANT)
    return -1;

  // We found a match. First append the less complex operand as a term.
  auto op = bitw->children[cIdx == 1 ? 0 : 1]->getCopy();
  op->multiply(factor);
  children.push_back(op);

  // Transform the bitwise expression.
  if (!toXor || bitw->type == NodeType::CONJUNCTION)
    factor = -factor;
  bitw->type = bitw->getTransformedBitwiseType(toXor);
  if (modRedValue(factor - 1, bitCount) != 0)
    bitw->multiply(factor);
  children[idx] = bitw;

  // Remove the children which we have merged into the bitwise expression.
  for (int j = static_cast<int>(indices.size()) - 1; j >= 0; --j) {
    children.erase(children.begin() + indices[j]);
    if (indices[j] < idx)
      idx -= 1;
  }

  // If the difference is not exactly zero, but constant, add this constant.
  if (!diff->isConstant(0)) {
    if (!children.empty() && children[0]->type != NodeType::CONSTANT)
      idx += 1;
    addConstant(constLow(*diff, diff->constant));

    // Check whether the sum's constant is now zero.
    if (!children.empty() && children[0]->isConstant(0)) {
      children.erase(children.begin());
      idx -= 1;
    }
  }

  return idx;
}

// ===================================================== disj involving xor in sums
bool Node::checkDisjInvolvingXorInSums() {
  if (type != NodeType::SUM)
    return false;

  bool changed = false;

  for (auto &child : children) {
    int64_t factor = 1;
    auto node = child;

    if (node->type == NodeType::PRODUCT) {
      if (node->children.size() != 2)
        continue;
      if (node->children[0]->type != NodeType::CONSTANT)
        continue;

      factor = constLow(*node, node->children[0]->constant);
      node = node->children[1];
    }

    if (node->type != NodeType::INCL_DISJUNCTION)
      continue;
    if (node->children.size() != 2)
      continue;

    int xorIdx = -1;
    if (node->children[0]->type == NodeType::EXCL_DISJUNCTION)
      xorIdx = 0;
    if (node->children[1]->type == NodeType::EXCL_DISJUNCTION) {
      if (xorIdx != -1)
        continue;
      xorIdx = 1;
    }
    if (xorIdx == -1)
      continue;

    int oIdx = xorIdx == 0 ? 1 : 0;
    auto xnode = node->children[xorIdx];
    auto o = node->children[oIdx];

    if (xnode->children.size() != 2)
      continue;

    if (o->equals(*xnode->children[0])) {
      o = newNodeWithChildren(NodeType::CONJUNCTION,
                              {o->getShallowCopy(), xnode->children[1]->getCopy()});
    } else if (o->equals(*xnode->children[1])) {
      o = newNodeWithChildren(NodeType::CONJUNCTION,
                              {o->getShallowCopy(), xnode->children[0]->getCopy()});
    } else if (o->type != NodeType::CONJUNCTION) {
      continue;
    } else {
      bool found0 = false;
      bool found1 = false;

      for (auto &ch : o->children) {
        if (ch->equals(*xnode->children[0]))
          found0 = true;
        else if (ch->equals(*xnode->children[1]))
          found1 = true;

        if (found0 && found1)
          break;
      }

      if (found0) {
        if (!found1)
          o->children.push_back(xnode->children[1]->getCopy());
      } else if (found1) {
        o->children.push_back(xnode->children[0]->getCopy());
      } else {
        continue;
      }
    }

    // Make sure that constants are in first child.
    o->inspectConstants();

    // If we are here, we know that we can transform the child.
    changed = true;

    if (factor == 1) {
      children.push_back(xnode->getShallowCopy());
    } else {
      auto prod = newNodeWithChildren(NodeType::PRODUCT,
                                      {newConstantNode(factor), xnode->getShallowCopy()});
      children.push_back(prod);
    }
    node->copy(*o);
  }

  return changed;
}

// ===================================================== xor involving disj
bool Node::checkXorInvolvingDisj() {
  if (type != NodeType::EXCL_DISJUNCTION)
    return false;
  if (children.size() != 2)
    return false;

  for (int disjIdx : {0, 1}) {
    auto disj = children[disjIdx];
    if (disj->type != NodeType::INCL_DISJUNCTION)
      continue;

    int oIdx = disjIdx == 0 ? 1 : 0;
    auto other = children[oIdx];
    int idx = disj->getIndexOfChild(other);
    // The disjunction does not contain the xor's other operand as an operand.
    if (idx == -1)
      continue;

    other->negate();
    type = NodeType::CONJUNCTION;

    disj->children.erase(disj->children.begin() + idx);
    if (disj->children.size() == 1)
      children[disjIdx] = disj->children[0];

    return true;
  }

  return false;
}

// ===================================================== negative bitwise inverse
bool Node::checkNegativeBitwInverse() {
  if (type != NodeType::PRODUCT)
    return false;
  if (children.size() != 2)
    return false;
  if (!children[0]->isConstant(-1))
    return false;

  auto node = children[1];
  if (node->type != NodeType::CONJUNCTION && node->type != NodeType::INCL_DISJUNCTION)
    return false;
  // TODO: We can consider all but one operand as a large single operand.
  if (node->children.size() != 2)
    return false;
  if (node->children[0]->type == NodeType::CONSTANT)
    return false;

  auto inv = node->children[0]->getCopy();
  inv->multiplyByMinusOne();
  if (!inv->equals(*node->children[1]))
    return false;

  if (node->type == NodeType::CONJUNCTION)
    node->type = NodeType::INCL_DISJUNCTION;
  else
    node->type = NodeType::CONJUNCTION;

  copy(*node);
  return true;
}

// ===================================================== xor pairs with constants
bool Node::checkXorPairsWithConstants() {
  if (type != NodeType::SUM)
    return false;

  auto l = collectIndicesOfBitwWithConstantsInSum(NodeType::EXCL_DISJUNCTION);
  if (l.empty())
    return false;

  std::vector<int> toRemove;
  int64_t const_ = 0;

  for (auto &pair : l) {
    int64_t factor = pair.first;
    auto &sublist = pair.second;
    std::vector<bool> done(sublist.size(), false);

    for (int i = static_cast<int>(sublist.size()) - 1; i > 0; --i) {
      if (done[i])
        continue;

      for (int j = 0; j < i; ++j) {
        if (done[j])
          continue;

        int firstIdx = sublist[i];
        auto first = children[firstIdx];
        if (first->type == NodeType::PRODUCT)
          first = first->children[1];

        int secIdx = sublist[j];
        auto second = children[secIdx];
        if (second->type == NodeType::PRODUCT)
          second = second->children[1];

        int64_t firstConst = modRedValue(constLow(*first, first->children[0]->constant), bitCount);
        int64_t secConst = modRedValue(constLow(*second, second->children[0]->constant), bitCount);
        // The set bits are not disjunct.
        if ((firstConst & secConst) != 0)
          continue;

        auto res = mergeBitwiseTerms(firstIdx, secIdx, first, second, factor,
                                     constLow(*first, first->children[0]->constant),
                                     constLow(*second, second->children[0]->constant));
        const_ += std::get<2>(res);

        // Make sure that this node is not used any more since it is now a
        // conjunction.
        done[j] = true;
        if (std::get<1>(res))
          toRemove.push_back(secIdx);
      }
    }
  }

  // Nothing has changed.
  if (toRemove.empty())
    return false;

  // Remove all children that have been merged into others.
  std::sort(toRemove.begin(), toRemove.end());
  for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
    children.erase(children.begin() + *it);

  // Adapt the constant.
  if (!children.empty() && children[0]->type == NodeType::CONSTANT) {
    children[0]->setAndReduceConstant(constLow(*children[0], children[0]->constant) + const_);
  } else {
    children.insert(children.begin(), newConstantNode(const_));
  }
  if (children.size() > 1 && !children.empty() && children[0]->isConstant(0))
    children.erase(children.begin());

  return true;
}

std::vector<std::pair<int64_t, std::vector<int>>>
Node::collectIndicesOfBitwWithConstantsInSum(NodeType expType) {
  // A list containing tuples of factors and lists of indices.
  std::vector<std::pair<int64_t, std::vector<int>>> l;
  for (int i = 0; i < static_cast<int>(children.size()); ++i) {
    auto fn = children[i]->getFactorOfBitwWithConstant(expType);
    if (fn.second == nullptr)
      continue;

    int64_t factor = fn.first;
    const Node *node = fn.second;

    bool found = false;
    for (auto &pair : l) {
      if (factor != pair.first)
        continue;

      auto &sublist = pair.second;
      int firstIdx = sublist[0];
      auto first = children[firstIdx];
      if (first->type == NodeType::PRODUCT)
        first = first->children[1];

      if (node->children.size() != first->children.size())
        continue;
      if (!doChildrenMatch(rest(node),
                           std::vector<std::shared_ptr<Node>>(first->children.begin() + 1, first->children.end())))
        continue;

      sublist.push_back(i);
      found = true;
      break;
    }

    if (!found)
      l.push_back({factor, {i}});
  }

  return l;
}

std::pair<int64_t, const Node *>
Node::getFactorOfBitwWithConstant(NodeType expType) const {
  int64_t factor = 0;
  const Node *node = nullptr;

  if (isBitwiseBinop()) {
    if (expType != NodeType::CONSTANT && type != expType)
      return {0, nullptr};

    factor = 1;
    // Python returns `self` here; the caller keeps it alive via the parent's
    // children list.
    node = this;

  } else if (type != NodeType::PRODUCT) {
    return {0, nullptr};
  } else if (children.size() != 2) {
    return {0, nullptr};
  } else if (children[0]->type != NodeType::CONSTANT) {
    return {0, nullptr};
  } else if (!children[1]->isBitwiseBinop()) {
    return {0, nullptr};
  } else if (expType != NodeType::CONSTANT && children[1]->type != expType) {
    return {0, nullptr};
  } else {
    factor = constFactor(*this, children[0]->constant);
    node = children[1].get();
  }

  if (node->children.empty() || node->children[0]->type != NodeType::CONSTANT)
    return {0, nullptr};

  return {factor, node};
}

// ===================================================== bitwise pairs with constants
bool Node::checkBitwPairsWithConstants() {
  if (type != NodeType::SUM)
    return false;

  bool changed = false;
  for (bool conj : {true, false}) {
    if (checkBitwPairsWithConstantsImpl(conj)) {
      changed = true;
      if (type != NodeType::SUM)
        return true;
    }
  }

  return changed;
}

bool Node::checkBitwPairsWithConstantsImpl(bool conj) {
  NodeType expType = conj ? NodeType::CONJUNCTION : NodeType::INCL_DISJUNCTION;
  auto l = collectIndicesOfBitwWithConstantsInSum(expType);
  if (l.empty())
    return false;

  std::vector<int> toRemove;
  bool changed = false;

  for (auto &pair : l) {
    int64_t factor = pair.first;
    auto &sublist = pair.second;

    for (int i = static_cast<int>(sublist.size()) - 1; i > 0; --i) {
      for (int j = 0; j < i; ++j) {
        int firstIdx = sublist[j];
        auto first = children[firstIdx];
        if (first->type == NodeType::PRODUCT)
          first = first->children[1];

        int secIdx = sublist[i];
        auto second = children[secIdx];
        if (second->type == NodeType::PRODUCT)
          second = second->children[1];

        int64_t firstConst = modRedValue(constLow(*first, first->children[0]->constant), bitCount);
        int64_t secConst = modRedValue(constLow(*second, second->children[0]->constant), bitCount);
        // The set bits are not disjunct.
        if ((firstConst & secConst) != 0)
          continue;

        auto res = mergeBitwiseTerms(firstIdx, secIdx, first, second, factor, firstConst, secConst);
        if (std::get<1>(res))
          toRemove.push_back(secIdx);

        changed = true;
        break;
      }
    }
  }

  // Nothing has changed.
  if (!changed)
    return false;

  // Remove all children that have been merged into others.
  std::sort(toRemove.begin(), toRemove.end());
  for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
    children.erase(children.begin() + *it);

  if (children.size() == 1)
    copy(*children[0]);
  return true;
}

// ===================================================== diff bitwise pairs with constants
bool Node::checkDiffBitwPairsWithConstants() {
  if (type != NodeType::SUM)
    return false;

  auto l = collectAllIndicesOfBitwWithConstants();
  if (l.empty())
    return false;

  std::vector<int> toRemove;
  int64_t const_ = 0;
  bool changed = false;

  for (auto &sublist : l) {
    for (int i = static_cast<int>(sublist.size()) - 1; i > 0; --i) {
      for (int j = 0; j < i; ++j) {
        int64_t firstFactor = sublist[j].first;
        int firstIdx = sublist[j].second;
        auto first = children[firstIdx];
        if (first->type == NodeType::PRODUCT)
          first = first->children[1];

        int64_t secFactor = sublist[i].first;
        int secIdx = sublist[i].second;
        auto second = children[secIdx];
        if (second->type == NodeType::PRODUCT)
          second = second->children[1];

        // This case has already been handled previously.
        if (first->type == second->type)
          continue;

        int64_t firstConst = modRedValue(constLow(*first, first->children[0]->constant), bitCount);
        int64_t secConst = modRedValue(constLow(*second, second->children[0]->constant), bitCount);
        // The set bits are not disjunct.
        if ((firstConst & secConst) != 0)
          continue;

        // Check the factors.
        int64_t factor;
        if (!getFactorForMergingBitwise(firstFactor, secFactor, first->type, second->type, factor))
          continue;

        auto res = mergeBitwiseTerms(firstIdx, secIdx, first, second, factor, firstConst, secConst);
        const_ += std::get<2>(res);

        if (std::get<1>(res))
          toRemove.push_back(secIdx);
        // Finally adapt the factor in the list.
        sublist[j].first = std::get<0>(res);

        changed = true;
        break;
      }
    }
  }

  // Nothing has changed.
  if (!changed)
    return false;

  // Remove all children that have been merged into others.
  std::sort(toRemove.begin(), toRemove.end());
  for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
    children.erase(children.begin() + *it);

  // Adapt the constant.
  if (!children.empty() && children[0]->type == NodeType::CONSTANT) {
    children[0]->setAndReduceConstant(constLow(*children[0], children[0]->constant) + const_);
  } else {
    children.insert(children.begin(), newConstantNode(const_));
  }
  if (children.size() > 1 && !children.empty() && children[0]->isConstant(0))
    children.erase(children.begin());

  if (children.size() == 1)
    copy(*children[0]);
  return true;
}

std::vector<std::vector<std::pair<int64_t, int>>> Node::collectAllIndicesOfBitwWithConstants() {
  // A list containing tuples of factors and lists of indices.
  std::vector<std::vector<std::pair<int64_t, int>>> l;

  for (int i = 0; i < static_cast<int>(children.size()); ++i) {
    auto fn = children[i]->getFactorOfBitwWithConstant();
    if (fn.second == nullptr)
      continue;

    int64_t factor = fn.first;
    auto node = fn.second;

    bool found = false;
    for (auto &sublist : l) {
      int firstIdx = sublist[0].second;
      auto first = children[firstIdx];
      if (first->type == NodeType::PRODUCT)
        first = first->children[1];

      if (node->children.size() == 2) {
        if (first->children.size() == 2) {
          if (!node->children[1]->equals(*first->children[1]))
            continue;
        } else {
          if (node->children[1]->type != first->type)
            continue;
          if (!doChildrenMatch(std::vector<std::shared_ptr<Node>>(node->children[1]->children),
                               std::vector<std::shared_ptr<Node>>(first->children.begin() + 1, first->children.end())))
            continue;
        }
      } else if (first->children.size() == 2) {
        if (first->children[1]->type != node->type)
          continue;
        if (!doChildrenMatch(std::vector<std::shared_ptr<Node>>(first->children[1]->children),
                             std::vector<std::shared_ptr<Node>>(node->children.begin() + 1, node->children.end())))
          continue;
      } else if (!doChildrenMatch(std::vector<std::shared_ptr<Node>>(first->children.begin() + 1, first->children.end()),
                                  std::vector<std::shared_ptr<Node>>(node->children.begin() + 1, node->children.end()))) {
        continue;
      }

      sublist.push_back({factor, i});
      found = true;
      break;
    }

    if (!found)
      l.push_back({{factor, i}});
  }

  return l;
}

bool Node::getFactorForMergingBitwise(int64_t fac1, int64_t fac2, NodeType type1, NodeType type2,
                                     int64_t &out) const {
  if (type1 == type2) {
    if (modRedValue(fac1 - fac2, bitCount) != 0)
      return false;
    out = fac1;
    return true;
  }

  if (type1 == NodeType::EXCL_DISJUNCTION) {
    if (type2 == NodeType::CONJUNCTION) {
      if (modRedValue(2 * fac1 + fac2, bitCount) != 0)
        return false;
    } else {
      if (modRedValue(2 * fac1 - fac2, bitCount) != 0)
        return false;
    }
    out = fac1;
    return true;
  }

  if (type1 == NodeType::CONJUNCTION) {
    if (type2 == NodeType::EXCL_DISJUNCTION) {
      if (modRedValue(fac1 + 2 * fac2, bitCount) != 0)
        return false;
    } else {
      if (modRedValue(fac1 + fac2, bitCount) != 0)
        return false;
    }
    out = fac2;
    return true;
  }

  if (type2 == NodeType::EXCL_DISJUNCTION) {
    if (modRedValue(-fac1 + 2 * fac2, bitCount) != 0)
      return false;
    out = fac2;
    return true;
  }
  if (modRedValue(fac1 + fac2, bitCount) != 0)
    return false;
  out = fac1;
  return true;
}

std::tuple<int64_t, bool, int64_t> Node::mergeBitwiseTerms(
    int firstIdx, int secIdx, const std::shared_ptr<Node> &first,
    const std::shared_ptr<Node> &second, int64_t factor, int64_t firstConst,
    int64_t secConst) {
  auto res = mergeBitwiseTermsAndGetOpfactor(firstIdx, secIdx, first, second, factor,
                                             firstConst, secConst);
  int64_t bitwFactor = std::get<0>(res);
  int64_t add = std::get<1>(res);
  int64_t opfac = std::get<2>(res);
  if (opfac == 0)
    return {bitwFactor, true, add};

  if (second->children.size() == 2)
    children[secIdx] = second->children[1];
  else {
    children[secIdx] = second->getShallowCopy();
    children[secIdx]->children.erase(children[secIdx]->children.begin());
  }

  if (opfac != 1)
    children[secIdx]->multiply(opfac);
  return {bitwFactor, false, add};
}

std::tuple<int64_t, int64_t, int64_t> Node::mergeBitwiseTermsAndGetOpfactor(
    int firstIdx, int secIdx, const std::shared_ptr<Node> &first,
    const std::shared_ptr<Node> &second, int64_t factor, int64_t firstConst,
    int64_t secConst) {
  int64_t constSum = firstConst + secConst;
  int64_t bitwFactor = getBitwiseFactorForMergingBitwise(factor, first->type, second->type);
  auto op = getOperandFactorAndConstantForMergingBitwise(factor, first->type, second->type,
                                                        firstConst, secConst);
  int64_t opfac = op.first;
  int64_t add = op.second;

  bool hasFactor = children[firstIdx]->type == NodeType::PRODUCT;
  // We modify the first child and mark the second for removal, or replace it
  // by the operand.
  first->children[0]->setAndReduceConstant(
      getConstOperandForMergingBitwise(constSum, first->type, second->type));
  if (first->type != second->type || first->type != NodeType::INCL_DISJUNCTION) {
    // If we have more operands, we have to reorganize everything before we
    // change the type.
    if (first->type != NodeType::CONJUNCTION && first->children.size() > 2) {
      auto node = newNode(first->type);
      node->children.push_back(first->children[0]->getShallowCopy());
      first->children.erase(first->children.begin());
      node->children.push_back(first->getShallowCopy());
      first->copy(*node);
    }

    first->type = NodeType::CONJUNCTION;
  }

  if (hasFactor) {
    if (bitwFactor == 1)
      children[firstIdx] = first;
    else
      children[firstIdx]->children[0]->setAndReduceConstant(bitwFactor);
  } else if (bitwFactor != 1) {
    auto factorNode = newConstantNode(bitwFactor);
    auto prod =
        newNodeWithChildren(NodeType::PRODUCT, {factorNode, first->getShallowCopy()});
    children[firstIdx]->copy(*prod);
  }

  return {bitwFactor, add, opfac};
}

int64_t Node::getConstOperandForMergingBitwise(int64_t constSum, NodeType type1,
                                              NodeType type2) const {
  if (type1 == type2 && type1 != NodeType::EXCL_DISJUNCTION)
    return constSum;
  // Return ~(const1 + const2).
  return -constSum - 1;
}

int64_t Node::getBitwiseFactorForMergingBitwise(int64_t factor, NodeType type1,
                                               NodeType type2) const {
  if (type1 == NodeType::EXCL_DISJUNCTION || type2 == NodeType::EXCL_DISJUNCTION)
    return 2 * factor;
  return factor;
}

std::pair<int64_t, int64_t> Node::getOperandFactorAndConstantForMergingBitwise(
    int64_t factor, NodeType type1, NodeType type2, int64_t const1, int64_t const2) const {
  if (type1 == type2) {
    if (type1 == NodeType::CONJUNCTION)
      return {0, 0};
    if (type1 == NodeType::INCL_DISJUNCTION)
      return {factor, 0};
    return {0, (const1 + const2) * factor};
  }

  if (type1 == NodeType::EXCL_DISJUNCTION) {
    if (type2 == NodeType::CONJUNCTION)
      return {-factor, const1 * factor};
    return {factor, (const1 + 2 * const2) * factor};
  }

  if (type1 == NodeType::CONJUNCTION) {
    if (type2 == NodeType::EXCL_DISJUNCTION)
      return {-factor, const2 * factor};
    return {0, const2 * factor};
  }

  if (type2 == NodeType::EXCL_DISJUNCTION)
    return {factor, (2 * const1 + const2) * factor};
  return {0, const1 * factor};
}

// ===================================================== bitwise tuples with constants
bool Node::checkBitwTuplesWithConstants() {
  if (type != NodeType::SUM)
    return false;

  auto l = collectAllIndicesOfBitwWithConstants();
  if (l.empty())
    return false;

  std::vector<int> toRemove;
  int64_t const_ = 0;
  bool changed = false;

  for (auto &sublist : l) {
    for (int i = static_cast<int>(sublist.size()) - 1; i > 1; --i) {
      int64_t add = 0;
      if (tryMergeBitwiseWithConstantsWith2Others(sublist, i, toRemove, add)) {
        changed = true;
        const_ += add;
      }
    }
  }

  // Nothing has changed.
  if (!changed)
    return false;

  // Remove all children that have been merged into others.
  std::sort(toRemove.begin(), toRemove.end());
  for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
    children.erase(children.begin() + *it);

  // Adapt the constant.
  if (!children.empty() && children[0]->type == NodeType::CONSTANT) {
    children[0]->setAndReduceConstant(constLow(*children[0], children[0]->constant) + const_);
  } else {
    children.insert(children.begin(), newConstantNode(const_));
  }
  if (children.size() > 1 && !children.empty() && children[0]->isConstant(0))
    children.erase(children.begin());

  if (children.size() == 1)
    copy(*children[0]);
  return true;
}

bool Node::tryMergeBitwiseWithConstantsWith2Others(
    std::vector<std::pair<int64_t, int>> &sublist, int i, std::vector<int> &toRemove,
    int64_t &add) {
  for (int j = 1; j < i; ++j) {
    for (int k = 0; k < j; ++k) {
      if (tryMergeTripleBitwiseWithConstants(sublist, i, j, k, toRemove, add))
        return true;
    }
  }

  return false;
}

bool Node::tryMergeTripleBitwiseWithConstants(
    std::vector<std::pair<int64_t, int>> &sublist, int i, int j, int k,
    std::vector<int> &toRemove, int64_t &add) {
  std::vector<std::array<int, 3>> perms = {{i, j, k}, {j, i, k}, {k, i, j}};
  for (auto &perm : perms) {
    int64_t mainFactor = sublist[perm[0]].first;
    int mainIdx = sublist[perm[0]].second;
    auto main = children[mainIdx];
    if (main->type == NodeType::PRODUCT)
      main = main->children[1];
    int64_t mainConst = modRedValue(constLow(*main, main->children[0]->constant), bitCount);

    int64_t firstFactor = sublist[perm[1]].first;
    int firstIdx = sublist[perm[1]].second;
    auto first = children[firstIdx];
    if (first->type == NodeType::PRODUCT)
      first = first->children[1];
    int64_t firstConst = modRedValue(constLow(*first, first->children[0]->constant), bitCount);

    int64_t secFactor = sublist[perm[2]].first;
    int secIdx = sublist[perm[2]].second;
    auto second = children[secIdx];
    if (second->type == NodeType::PRODUCT)
      second = second->children[1];
    int64_t secConst = modRedValue(constLow(*second, second->children[0]->constant), bitCount);

    // Get factors for merging.
    int64_t factor1 = 0, factor2 = 0;
    if (!getFactorsForMergingTriple(first->type, second->type, main->type, firstFactor, secFactor,
                                    mainFactor, firstConst, secConst, mainConst, factor1, factor2)) {
      continue;
    }

    int i1 = perm[1];
    // We can merge the triple. Rearrange it such that the one that vanishes
    // has the highest index i.
    if (perm[0] != i) {
      // In the Python source this is an assert; mirror the swap.
      auto tmp = sublist[perm[0]];
      sublist[perm[0]] = sublist[perm[1]];
      sublist[perm[1]] = tmp;
      i1 = perm[0];
    }

    auto res1 = mergeBitwiseTermsAndGetOpfactor(firstIdx, mainIdx, first, main, factor1,
                                                firstConst, mainConst);
    auto res2 = mergeBitwiseTermsAndGetOpfactor(secIdx, mainIdx, second, main, factor2,
                                                secConst, mainConst);
    int64_t opfac = modRedValue(std::get<2>(res1) + std::get<2>(res2), bitCount);

    if (opfac == 0) {
      toRemove.push_back(mainIdx);
    } else {
      children[mainIdx] = main->children[1];
      if (opfac != 1)
        children[mainIdx]->multiply(opfac);
    }

    // Finally adapt the factors in the list.
    sublist[i1].first = std::get<0>(res1);
    sublist[perm[2]].first = std::get<0>(res2);

    add = std::get<1>(res1) + std::get<1>(res2);
    return true;
  }

  return false;
}

bool Node::getFactorsForMergingTriple(NodeType type1, NodeType type2, NodeType type0,
                                     int64_t fac1, int64_t fac2, int64_t fac0, int64_t const1,
                                     int64_t const2, int64_t const0, int64_t &factor1,
                                     int64_t &factor2) const {
  // Check whether the constants' 1 positions are disjunct.
  if ((const1 & const0) != 0)
    return false;
  if ((const2 & const0) != 0)
    return false;

  // Check whether the factors fit.
  if (!getPossibleFactorForMergingBitwise(fac1, type1, type0, factor1))
    return false;
  if (!getPossibleFactorForMergingBitwise(fac2, type2, type0, factor2))
    return false;

  // We cannot merge this triple.
  if (modRedValue(factor1 + factor2 - fac0, bitCount) != 0)
    return false;

  if (!getFactorForMergingBitwise(fac1, factor1, type1, type0, factor1))
    return false;
  if (!getFactorForMergingBitwise(fac2, factor2, type2, type0, factor2))
    return false;
  return true;
}

bool Node::getPossibleFactorForMergingBitwise(int64_t fac1, NodeType type1, NodeType type0,
                                             int64_t &out) const {
  if (type1 == NodeType::EXCL_DISJUNCTION) {
    if (type0 == NodeType::CONJUNCTION) {
      out = -2 * fac1;
      return true;
    }
    if (type0 == NodeType::INCL_DISJUNCTION) {
      out = 2 * fac1;
      return true;
    }
    out = fac1;
    return true;
  }

  if (type1 == NodeType::CONJUNCTION) {
    if (type0 == NodeType::EXCL_DISJUNCTION) {
      if (fac1 % 2 != 0)
        return false;
      out = -fac1 / 2;
      return true;
    }
    if (type0 == NodeType::CONJUNCTION) {
      out = fac1;
      return true;
    }
    out = -fac1;
    return true;
  }

  if (type0 == NodeType::EXCL_DISJUNCTION) {
    if (fac1 % 2 != 0)
      return false;
    out = fac1 / 2;
    return true;
  }
  if (type0 == NodeType::CONJUNCTION) {
    out = -fac1;
    return true;
  }
  out = fac1;
  return true;
}

// ===================================================== bitwise pairs with inverses
bool Node::checkBitwPairsWithInverses() {
  if (type != NodeType::SUM)
    return false;

  bool changed = false;
  for (NodeType expType : {NodeType::CONJUNCTION, NodeType::EXCL_DISJUNCTION,
                           NodeType::INCL_DISJUNCTION}) {
    if (checkBitwPairsWithInversesImpl(expType)) {
      changed = true;
      if (type != NodeType::SUM)
        return true;
    }
  }

  return changed;
}

bool Node::checkBitwPairsWithInversesImpl(NodeType expType) {
  auto l = collectIndicesOfBitwWithoutConstantsInSum(expType);
  if (l.empty())
    return false;

  std::vector<int> toRemove;
  bool changed = false;
  int64_t const_ = 0;

  for (auto &triple : l) {
    int64_t factor = std::get<0>(triple);
    auto &sublist = std::get<2>(triple);
    std::vector<bool> done(sublist.size(), false);

    for (int i = static_cast<int>(sublist.size()) - 1; i > 0; --i) {
      if (done[i])
        continue;

      for (int j = 0; j < i; ++j) {
        if (done[j])
          continue;

        int firstIdx = sublist[j];
        auto first = children[firstIdx];
        if (first->type == NodeType::PRODUCT)
          first = first->children[1];

        int secIdx = sublist[i];
        auto second = children[secIdx];
        if (second->type == NodeType::PRODUCT)
          second = second->children[1];

        auto indices = first->getOnlyDifferingChildIndices(*second);
        // There are either no or too many differing children.
        if (indices.first == -1)
          continue;

        // The differing children are not inverse.
        if (!first->children[indices.first]->equalsNegated(*second->children[indices.second]))
          continue;

        auto res = mergeInverseBitwiseTerms(firstIdx, secIdx, first, second, factor, indices);
        const_ += std::get<2>(res);

        if (std::get<0>(res))
          toRemove.push_back(firstIdx);
        if (std::get<1>(res))
          toRemove.push_back(secIdx);

        // Make sure that this node is not used any more since it is in
        // general no bitwise expression any more.
        done[j] = true;
        changed = true;
        break;
      }
    }
  }

  // Nothing has changed.
  if (!changed)
    return false;

  // Remove all children that have been merged into others.
  if (!toRemove.empty()) {
    std::sort(toRemove.begin(), toRemove.end());
    for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
      children.erase(children.begin() + *it);
  }

  // Adapt the constant. It can even happen that all children have been removed.
  if (children.empty()) {
    copy(*newConstantNode(const_));
    return true;
  }

  if (children[0]->type == NodeType::CONSTANT) {
    children[0]->setAndReduceConstant(constLow(*children[0], children[0]->constant) + const_);
  } else {
    children.insert(children.begin(), newConstantNode(const_));
  }
  if (children.size() > 1 && children[0]->isConstant(0))
    children.erase(children.begin());

  if (children.size() == 1)
    copy(*children[0]);
  return true;
}

std::vector<std::tuple<int64_t, int, std::vector<int>>>
Node::collectIndicesOfBitwWithoutConstantsInSum(NodeType expType) {
  // A list containing tuples of factors and lists of indices.
  std::vector<std::tuple<int64_t, int, std::vector<int>>> l;
  for (int i = 0; i < static_cast<int>(children.size()); ++i) {
    auto fn = children[i]->getFactorOfBitwWithoutConstant(expType);
    if (fn.second == nullptr)
      continue;

    int64_t factor = fn.first;
    auto node = fn.second;

    int opCnt = static_cast<int>(node->children.size());
    bool found = false;
    for (auto &triple : l) {
      if (factor != std::get<0>(triple))
        continue;
      if (opCnt != std::get<1>(triple))
        continue;

      std::get<2>(triple).push_back(i);
      found = true;
      break;
    }

    if (!found)
      l.push_back({factor, opCnt, {i}});
  }

  return l;
}

std::pair<int64_t, const Node *>
Node::getFactorOfBitwWithoutConstant(NodeType expType) const {
  int64_t factor = 0;
  const Node *node = nullptr;

  if (isBitwiseBinop()) {
    if (expType != NodeType::CONSTANT && type != expType)
      return {0, nullptr};

    factor = 1;
    // Python returns `self` here; the caller keeps it alive via the parent's
    // children list.
    node = this;

  } else if (type != NodeType::PRODUCT) {
    return {0, nullptr};
  } else if (children.size() != 2) {
    return {0, nullptr};
  } else if (children[0]->type != NodeType::CONSTANT) {
    return {0, nullptr};
  } else if (!children[1]->isBitwiseBinop()) {
    return {0, nullptr};
  } else if (expType != NodeType::CONSTANT && children[1]->type != expType) {
    return {0, nullptr};
  } else {
    factor = constFactor(*this, children[0]->constant);
    node = children[1].get();
  }

  if (node->children.empty() || node->children[0]->type == NodeType::CONSTANT)
    return {0, nullptr};

  return {factor, node};
}

std::pair<int, int> Node::getOnlyDifferingChildIndices(const Node &other) const {
  if (type == other.type) {
    // In this case the nodes would have to have same child count.
    if (children.size() != other.children.size())
      return {-1, -1};
    return getOnlyDifferingChildIndicesSameLen(other);
  }

  if (children.size() == other.children.size()) {
    if (children.size() != 2)
      return {-1, -1};
    return getOnlyDifferingChildIndicesSameLen(other);
  }

  if (children.size() < other.children.size()) {
    if (children.size() != 2)
      return {-1, -1};
    return getOnlyDifferingChildIndicesDiffLen(other);
  }

  if (other.children.size() != 2)
    return {-1, -1};

  auto indices = other.getOnlyDifferingChildIndicesDiffLen(*this);
  if (indices.first == -1)
    return {-1, -1};
  return {indices.second, indices.first};
}

std::pair<int, int> Node::getOnlyDifferingChildIndicesSameLen(const Node &other) const {
  int idx1 = -1;

  std::vector<int> oIndices;
  for (int j = 0; j < static_cast<int>(other.children.size()); ++j)
    oIndices.push_back(j);
  for (int i = 0; i < static_cast<int>(children.size()); ++i) {
    auto &child = children[i];
    bool found = false;
    // Mirror Python list-iteration-with-removal semantics.
    size_t p = 0;
    while (p < oIndices.size()) {
      int j = oIndices[p];
      if (child->equals(*other.children[j])) {
        oIndices.erase(oIndices.begin() + p);
        found = true;
      } else {
        ++p;
      }
    }

    if (!found) {
      if (idx1 == -1)
        idx1 = i;
      // This is already the second child that does not appear in other's
      // children.
      else
        return {-1, -1};
    }
  }

  // There are no differing children.
  if (idx1 == -1)
    return {-1, -1};

  return {idx1, oIndices[0]};
}

std::pair<int, int> Node::getOnlyDifferingChildIndicesDiffLen(const Node &other) const {
  for (int i : {0, 1}) {
    int idx = other.getIndexOfChildNegated(children[i]);
    if (idx == -1)
      continue;

    int oi = i == 0 ? 1 : 0;

    if (children[oi]->type != other.type)
      continue;
    std::vector<std::shared_ptr<Node>> rest;
    for (int j = 0; j < static_cast<int>(other.children.size()); ++j)
      if (j != idx)
        rest.push_back(other.children[j]);
    if (!doChildrenMatch(children[oi]->children, rest))
      continue;

    return {i, idx};
  }

  return {-1, -1};
}

std::tuple<bool, bool, int64_t> Node::mergeInverseBitwiseTerms(
    int firstIdx, int secIdx, const std::shared_ptr<Node> &first,
    const std::shared_ptr<Node> &second, int64_t factor,
    const std::pair<int, int> &indices) {
  NodeType type1 = first->type;
  NodeType type2 = second->type;
  auto fac = getOperandFactorsAndConstantForMergingInverseBitwise(factor, type1, type2);
  int64_t invOpFac = std::get<0>(fac);
  int64_t sameOpFac = std::get<1>(fac);
  int64_t add = std::get<2>(fac);

  bool removeFirst = sameOpFac == 0;
  if (!removeFirst) {
    bool hasFactor = children[firstIdx]->type == NodeType::PRODUCT;

    if (first->children.size() == 2) {
      int oIdx = indices.first == 1 ? 0 : 1;
      first->copy(*first->children[oIdx]);
    } else {
      first->children.erase(first->children.begin() + indices.first);
    }

    if (hasFactor) {
      if (sameOpFac == 1)
        children[firstIdx] = first;
      else
        children[firstIdx]->children[0]->setAndReduceConstant(sameOpFac);
    } else if (sameOpFac != 1) {
      auto factorNode = newConstantNode(sameOpFac);
      auto prod = newNodeWithChildren(NodeType::PRODUCT,
                                      {factorNode, first->getShallowCopy()});
      children[firstIdx]->copy(*prod);
    }

    // Flatten the node if necessary.
    children[firstIdx]->flatten();
  }

  bool removeSecond = invOpFac == 0;
  if (!removeSecond) {
    bool hasFactor = children[secIdx]->type == NodeType::PRODUCT;

    second->copy(*second->children[indices.second]);
    if (mustInvertAtMergingInverseBitwise(type1, type2))
      second->negate();

    if (hasFactor) {
      if (invOpFac == 1)
        children[secIdx] = second;
      else
        children[secIdx]->children[0]->setAndReduceConstant(invOpFac);
    } else if (invOpFac != 1) {
      auto factorNode = newConstantNode(invOpFac);
      auto prod = newNodeWithChildren(NodeType::PRODUCT,
                                      {factorNode, second->getShallowCopy()});
      children[secIdx]->copy(*prod);
    }

    // Flatten the node if necessary.
    children[secIdx]->flatten();
  }

  return {removeFirst, removeSecond, add};
}

std::tuple<int64_t, int64_t, int64_t>
Node::getOperandFactorsAndConstantForMergingInverseBitwise(int64_t factor, NodeType type1,
                                                         NodeType type2) const {
  if (type1 == type2) {
    if (type1 == NodeType::CONJUNCTION)
      return {0, factor, 0};
    if (type1 == NodeType::INCL_DISJUNCTION)
      return {0, factor, -factor};
    return {0, 0, -factor};
  }

  if (type1 == NodeType::EXCL_DISJUNCTION) {
    if (type2 == NodeType::CONJUNCTION)
      return {factor, -factor, 0};
    return {-factor, factor, -2 * factor};
  }

  if (type1 == NodeType::CONJUNCTION) {
    if (type2 == NodeType::EXCL_DISJUNCTION)
      return {factor, -factor, 0};
    return {factor, 0, 0};
  }

  if (type2 == NodeType::EXCL_DISJUNCTION)
    return {-factor, factor, -2 * factor};
  return {factor, 0, 0};
}

bool Node::mustInvertAtMergingInverseBitwise(NodeType type1, NodeType type2) const {
  if (type1 == NodeType::EXCL_DISJUNCTION)
    return true;
  if (type2 == NodeType::EXCL_DISJUNCTION)
    return false;
  return type1 == NodeType::INCL_DISJUNCTION;
}

// ===================================================== diff bitwise pairs with inverses
bool Node::checkDiffBitwPairsWithInverses() {
  if (type != NodeType::SUM)
    return false;

  auto l = collectAllIndicesOfBitwWithoutConstants();
  if (l.empty())
    return false;

  std::vector<int> toRemove;
  std::vector<bool> done(l.size(), false);
  bool changed = false;
  int64_t const_ = 0;

  for (int i = static_cast<int>(l.size()) - 1; i > 0; --i) {
    if (done[i])
      continue;

    for (int j = 0; j < i; ++j) {
      if (done[j])
        continue;

      int64_t firstFactor = l[j].first;
      int firstIdx = l[j].second;
      auto first = children[firstIdx];
      if (first->type == NodeType::PRODUCT)
        first = first->children[1];

      int64_t secFactor = l[i].first;
      int secIdx = l[i].second;
      auto second = children[secIdx];
      if (second->type == NodeType::PRODUCT)
        second = second->children[1];

      // This case has already been handled previously.
      if (first->type == second->type)
        continue;

      // Check the factors.
      int64_t factor;
      if (!getFactorForMergingBitwise(firstFactor, secFactor, first->type, second->type, factor))
        continue;

      auto indices = first->getOnlyDifferingChildIndices(*second);
      // There are either no or too many differing children.
      if (indices.first == -1)
        continue;

      // The differing children are not inverse.
      if (!first->children[indices.first]->equalsNegated(*second->children[indices.second]))
        continue;

      auto res = mergeInverseBitwiseTerms(firstIdx, secIdx, first, second, factor, indices);
      const_ += std::get<2>(res);

      if (std::get<0>(res))
        toRemove.push_back(firstIdx);
      if (std::get<1>(res))
        toRemove.push_back(secIdx);

      // Make sure that this node is not used any more since it is in general
      // no bitwise expression any more.
      done[j] = true;
      changed = true;
      break;
    }
  }

  // Nothing has changed.
  if (!changed)
    return false;

  // Remove all children that have been merged into others.
  if (!toRemove.empty()) {
    std::sort(toRemove.begin(), toRemove.end());
    for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
      children.erase(children.begin() + *it);
  }

  // Adapt the constant. It can even happen that all children have been removed.
  if (children.empty()) {
    copy(*newConstantNode(const_));
    return true;
  }

  if (children[0]->type == NodeType::CONSTANT) {
    children[0]->setAndReduceConstant(constLow(*children[0], children[0]->constant) + const_);
  } else {
    children.insert(children.begin(), newConstantNode(const_));
  }
  if (children.size() > 1 && children[0]->isConstant(0))
    children.erase(children.begin());

  if (children.size() == 1)
    copy(*children[0]);
  return true;
}

std::vector<std::pair<int64_t, int>> Node::collectAllIndicesOfBitwWithoutConstants() {
  // A list containing tuples of factors and indices.
  std::vector<std::pair<int64_t, int>> l;
  for (int i = 0; i < static_cast<int>(children.size()); ++i) {
    auto fn = children[i]->getFactorOfBitwWithoutConstant();
    if (fn.second == nullptr)
      continue;

    l.push_back({fn.first, i});
  }

  return l;
}

// ===================================================== bitwise and op in sum
bool Node::checkBitwAndOpInSum() {
  if (type != NodeType::SUM)
    return false;

  for (int bitwIdx = 0; bitwIdx < static_cast<int>(children.size()); ++bitwIdx) {
    auto bitw = children[bitwIdx];
    bool disj = bitw->type == NodeType::INCL_DISJUNCTION;

    if (!disj) {
      if (bitw->type != NodeType::PRODUCT)
        continue;
      if (bitw->children.empty() || !bitw->children[0]->isConstant(-1))
        continue;
      // The formula x - (x&y) -> x&~y only holds when the product is exactly
      // (-1)*(conjunction); any further factor invalidates it.
      if (bitw->children.size() != 2)
        continue;

      bitw = bitw->children[1];
      if (bitw->type != NodeType::CONJUNCTION)
        continue;
    }

    // If we are here, the bitwise has already been validated. Now check the
    // other term(s).

    std::shared_ptr<Node> other;
    if (children.size() == 2) {
      int oIdx = bitwIdx == 0 ? 1 : 0;
      other = children[oIdx]->getCopy();
    } else {
      other = getCopy();
      other->children.erase(other->children.begin() + bitwIdx);
    }

    if (disj)
      other->multiplyByMinusOne();
    int idx = bitw->getIndexOfChild(other);

    // The bitwise does not contain the other term (or its inverse, resp.) as
    // an operand.
    if (idx == -1)
      continue;

    // Finally transform the sum.
    copy(*bitw);

    // If the bitwise has more than 2 operands, we have to rearrange
    // everything.
    if (children.size() > 2) {
      auto node = bitw->getShallowCopy();
      node->children.erase(node->children.begin() + idx);
      auto neg = newNodeWithChildren(NodeType::NEGATION, {node});
      children = {children[idx]->getShallowCopy(), neg};
    } else {
      int oIdx = idx == 0 ? 1 : 0;
      children[oIdx]->negate();
    }

    if (disj)
      copy(*newNodeWithChildren(NodeType::NEGATION, {getShallowCopy()}));

    return true;
  }

  return false;
}

// ===================================================== insert xor in sum
bool Node::checkInsertXorInSum() {
  if (type != NodeType::SUM)
    return false;

  bool changed = false;
  int i = -1;
  while (true) {
    i += 1;
    if (i >= static_cast<int>(children.size()))
      break;

    auto first = children[i]->getCopy();
    first->multiplyByMinusOne();

    if (first->type != NodeType::CONJUNCTION && first->type != NodeType::PRODUCT)
      continue;
    if (first->type != NodeType::PRODUCT && first->children.size() != 2)
      continue;

    for (int j = 0; j < static_cast<int>(children.size()); ++j) {
      if (i == j)
        continue;

      auto disj = children[j];

      if (disj->type != NodeType::INCL_DISJUNCTION && disj->type != NodeType::PRODUCT)
        continue;
      if ((first->type == NodeType::PRODUCT) != (disj->type == NodeType::PRODUCT))
        continue;

      auto conj = first;
      if (conj->type == NodeType::PRODUCT) {
        auto indices = conj->getOnlyDifferingChildIndices(*disj);
        if (indices.first == -1)
          continue;

        conj = conj->children[indices.first];
        disj = disj->children[indices.second];

        if (conj->type != NodeType::CONJUNCTION)
          continue;
        if (disj->type != NodeType::INCL_DISJUNCTION)
          continue;
      }

      if (!doChildrenMatch(conj->children, disj->children))
        continue;

      disj->type = NodeType::EXCL_DISJUNCTION;
      children.erase(children.begin() + i);
      // Decrease i because it will be increased at the start of the next
      // iteration.
      i -= 1;

      changed = true;
      break;
    }
  }

  if (!changed)
    return false;

  if (children.size() == 1)
    copy(*children[0]);
  return true;
}

} // namespace MBA
} // namespace LSiMBA
