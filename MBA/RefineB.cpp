// GAMBA native C++ port — Refine batch B.
// Mirrors external/GAMBA/src/utils/node.py (bitwise-negation, power-of-two,
// beautify, rewrite-powers, factor-out rules).
#include "Node.h"

#include <algorithm>

namespace LSiMBA {
namespace MBA {

// Helper: a constant node holding 2^e (e in 0..63).
static std::shared_ptr<Node> pow2Node(const Node *self, int e) {
  auto n = self->newNode(NodeType::CONSTANT);
  n->constant = MBAValue(128, 1) << e;
  n->reduceConstant();
  return n;
}

// --------------------------------------------------------- bitwise negations
bool Node::checkBitwiseNegations(Node *parent) {
  if (type == NodeType::NEGATION) {
    if (parent != nullptr && parent->isBitwiseOp())
      return false;

    if (children[0]->type == NodeType::PRODUCT) {
      substituteBitwiseNegationProduct();
      return true;
    }
    if (children[0]->type == NodeType::SUM) {
      substituteBitwiseNegationSum();
      return true;
    }
    return substituteBitwiseNegationGeneric(parent);
  }

  if (parent == nullptr || !parent->isBitwiseOp())
    return false;

  auto child = getOptTransformedNegated();
  if (child == nullptr)
    return false;

  type = NodeType::NEGATION;
  children = {child};

  return true;
}

void Node::substituteBitwiseNegationProduct() {
  type = NodeType::SUM;
  children.insert(children.begin(), newConstantNode(-1));

  auto child = children[1];
  if (child->children[0]->type == NodeType::CONSTANT) {
    child->children[0]->constant = child->children[0]->getReducedConstant(-child->children[0]->constant);
  } else {
    child->children.insert(child->children.begin(), newConstantNode(-1));
  }
}

void Node::substituteBitwiseNegationSum() {
  type = NodeType::PRODUCT;
  children.insert(children.begin(), newConstantNode(-1));

  auto child = children[1];
  if (child->children[0]->type == NodeType::CONSTANT) {
    child->children[0]->constant = child->children[0]->getReducedConstant(child->children[0]->constant + MBAValue(128, 1));
  } else {
    child->children.insert(child->children.begin(), newConstantNode(1));
  }
}

bool Node::substituteBitwiseNegationGeneric(Node *parent) {
  if (parent == nullptr)
    return false;
  if (parent->type != NodeType::SUM && parent->type != NodeType::PRODUCT)
    return false;

  if (parent->type == NodeType::PRODUCT) {
    if (parent->children.size() > 2 || parent->children[0]->type != NodeType::CONSTANT)
      return false;
  }

  auto prod = newNodeWithChildren(NodeType::PRODUCT, {newConstantNode(-1), children[0]});

  type = NodeType::SUM;
  children = {newConstantNode(-1), prod};

  return true;
}

// --------------------------------------------------------- powers of two
bool Node::checkBitwisePowersOfTwo() {
  if (!isBitwiseBinop())
    return false;

  int e = getMaxFactorPowerOfTwoInChildren();
  if (e <= 0)
    return false;

  MBAValue c;
  bool hasC = children[0]->type == NodeType::CONSTANT;
  if (hasC)
    c = children[0]->constant;

  uint64_t add = 0;
  bool hasAdd = false;
  for (auto &child : children) {
    uint64_t rem = child->divideByPowerOfTwo(e);
    if (!hasAdd) {
      add = rem;
      hasAdd = true;
    } else {
      if (type == NodeType::CONJUNCTION)
        add &= rem;
      else if (type == NodeType::INCL_DISJUNCTION)
        add |= rem;
      else
        add ^= rem;
    }
  }

  auto prod = newNode(NodeType::PRODUCT);
  prod->children = {pow2Node(this, e), getShallowCopy()};
  copy(*prod);

  if (add != 0) {
    auto constNode = newConstantNode(static_cast<int64_t>(add));
    auto sumNode = newNodeWithChildren(NodeType::SUM, {constNode, getShallowCopy()});
    copy(*sumNode);
  }

  return true;
}

int Node::getMaxFactorPowerOfTwoInChildren(bool allowRem) {
  bool withNeg = allowRem && isBitwiseBinop();
  int maxe = children[0]->getMaxFactorPowerOfTwo(withNeg);

  if (allowRem && children[0]->type == NodeType::CONSTANT)
    maxe = -1;

  if (maxe == 0)
    return 0;

  for (size_t i = 1; i < children.size(); ++i) {
    int e = children[i]->getMaxFactorPowerOfTwo(withNeg);
    if (e == 0)
      return 0;
    if (e == -1)
      continue;
    maxe = (maxe == -1) ? e : std::min(maxe, e);
  }

  return maxe;
}

int Node::getMaxFactorPowerOfTwo(bool allowRem) {
  if (type == NodeType::CONSTANT)
    return static_cast<int>(MBAOps::trailingZeros(MBAOps::toLow64(constant, bitCount)));

  if (type == NodeType::PRODUCT)
    return children[0]->getMaxFactorPowerOfTwo(false);

  if (type == NodeType::SUM)
    return getMaxFactorPowerOfTwoInChildren(allowRem);

  if (allowRem && type == NodeType::NEGATION)
    return children[0]->getMaxFactorPowerOfTwo(false);

  return 0;
}

uint64_t Node::divideByPowerOfTwo(int e) {
  if (type == NodeType::CONSTANT) {
    MBAValue orig = constant;
    constant = constant.lshr(e);
    return MBAOps::toLow64(orig - constant.shl(e), bitCount);
  }

  if (type == NodeType::PRODUCT) {
    uint64_t rem = children[0]->divideByPowerOfTwo(e);
    (void)rem;
    if (children[0]->isConstant(1)) {
      children.erase(children.begin());
      if (children.size() == 1)
        copy(*children[0]);
    }
    return 0;
  }

  if (type == NodeType::SUM) {
    uint64_t add = 0;
    for (auto &child : children) {
      uint64_t rem = child->divideByPowerOfTwo(e);
      if (rem != 0)
        add = rem;
    }
    if (children[0]->isConstant(0)) {
      children.erase(children.begin());
      if (children.size() == 1)
        copy(*children[0]);
    }
    return add;
  }

  // NEGATION
  uint64_t rem = children[0]->divideByPowerOfTwo(e);
  (void)rem;
  return (1ULL << e) - 1;
}

// --------------------------------------------------------- beautify constants
bool Node::checkBeautifyConstantsInProducts() {
  if (type != NodeType::PRODUCT)
    return false;
  if (children[0]->type != NodeType::CONSTANT)
    return false;

  int e = static_cast<int>(MBAOps::trailingZeros(MBAOps::toLow64(children[0]->constant, bitCount)));
  if (e <= 0)
    return false;

  bool changed = false;
  for (size_t i = 1; i < children.size(); ++i)
    if (children[i]->checkBeautifyConstants(e))
      changed = true;

  return changed;
}

bool Node::checkBeautifyConstants(int e) {
  if (isBitwiseOp() || type == NodeType::SUM || type == NodeType::PRODUCT) {
    bool changed = false;
    for (auto &child : children)
      if (child->checkBeautifyConstants(e))
        changed = true;
    return changed;
  }

  if (type != NodeType::CONSTANT)
    return false;

  MBAValue orig = constant;
  uint64_t low = MBAOps::toLow64(constant, bitCount);

  uint64_t mask = MBAOps::widthMask(bitCount) >> e;
  uint64_t b = low & (1ULL << (bitCount - e - 1));

  constant = MBAValue(128, low & mask);

  if (b > 0) {
    if (MBAOps::popcount(low & mask) > 1 || b == 1)
      constant = MBAValue(128, (low & mask) | ~mask);
  }

  reduceConstant();
  return constant != orig;
}

// --------------------------------------------------------- move in negations
bool Node::checkMoveInBitwiseNegations() {
  if (type != NodeType::NEGATION)
    return false;

  NodeType childType = children[0]->type;
  if (childType == NodeType::EXCL_DISJUNCTION)
    return checkMoveInBitwiseNegationExclDisj();
  if (childType == NodeType::CONJUNCTION || childType == NodeType::INCL_DISJUNCTION)
    return checkMoveInBitwiseNegationConjOrInclDisj();

  return false;
}

bool Node::checkMoveInBitwiseNegationConjOrInclDisj() {
  auto child = children[0];
  if (!child->isAnyChildNegated())
    return false;

  child->negateAllChildren();
  child->type = (child->type == NodeType::CONJUNCTION) ? NodeType::INCL_DISJUNCTION : NodeType::CONJUNCTION;
  copy(*child);

  return true;
}

bool Node::isAnyChildNegated() {
  for (auto &child : children) {
    if (child->type == NodeType::NEGATION)
      return true;
    if (child->getOptTransformedNegated() != nullptr)
      return true;
  }
  return false;
}

void Node::negateAllChildren() {
  for (auto &child : children)
    child->negate();
}

void Node::negate() {
  if (type == NodeType::NEGATION) {
    copy(*children[0]);
    return;
  }

  auto node = getOptTransformedNegated();
  if (node != nullptr) {
    copy(*node);
    return;
  }

  auto n = newNodeWithChildren(NodeType::NEGATION, {getCopy()});
  copy(*n);
}

bool Node::checkMoveInBitwiseNegationExclDisj() {
  auto child = children[0];
  int depth = -1;
  auto n = child->getRecursivelyNegatedChild(&depth);
  if (n == nullptr)
    return false;

  n->negate();
  copy(*child);

  return true;
}

Node *Node::getRecursivelyNegatedChild(int *depth, int maxDepth) {
  if (type == NodeType::NEGATION) {
    if (depth)
      *depth = 0;
    return this;
  }

  if (getOptTransformedNegated() != nullptr) {
    if (depth)
      *depth = 0;
    return this;
  }

  if (maxDepth != -1 && maxDepth == 0)
    return nullptr;
  if (!isBitwiseBinop())
    return nullptr;

  Node *candidate = nullptr;
  int opt = -1;
  int nextMax = (maxDepth == -1) ? -1 : maxDepth - 1;
  for (auto &child : children) {
    int d = -1;
    child->getRecursivelyNegatedChild(&d, nextMax);
    if (d == -1)
      continue;

    if (maxDepth == -1) {
      if (depth)
        *depth = d + 1;
      return child.get();
    }

    opt = d;
    candidate = child.get();
    nextMax = opt - 1;
  }

  if (depth)
    *depth = opt;
  return candidate;
}

bool Node::isNegated() {
  if (type == NodeType::NEGATION)
    return true;
  return getOptTransformedNegated() != nullptr;
}

// --------------------------------------------------------- excl disjunction negations
bool Node::checkBitwiseNegationsInExclDisjunctions() {
  if (type != NodeType::EXCL_DISJUNCTION)
    return false;

  std::shared_ptr<Node> neg = nullptr;
  bool changed = false;

  for (auto &child : children) {
    if (!child->isNegated())
      continue;

    if (neg == nullptr) {
      neg = child;
      continue;
    }

    neg->negate();
    child->negate();
    neg = nullptr;
    changed = true;
  }

  return changed;
}

// --------------------------------------------------------- rewrite powers
bool Node::checkRewritePowers(Node *parent) {
  if (type != NodeType::POWER)
    return false;

  auto exp = children[1];
  if (exp->type != NodeType::CONSTANT)
    return false;

  auto base = children[0];
  if (base->type != NodeType::PRODUCT)
    return false;
  if (base->children[0]->type != NodeType::CONSTANT)
    return false;

  uint64_t constLow = power(MBAOps::toLow64(base->children[0]->constant, bitCount),
                           MBAOps::toLow64(exp->constant, bitCount));
  int64_t constVal = static_cast<int64_t>(constLow);
  base->children.erase(base->children.begin());
  if (base->children.size() == 1)
    base->copy(*base->children[0]);

  if (constLow == 1)
    return true;

  if (parent != nullptr && parent->type == NodeType::PRODUCT) {
    if (parent->children[0]->type == NodeType::PRODUCT) {
      parent->children[0]->constant = parent->children[0]->getReducedConstant(
          parent->children[0]->constant * MBAOps::fromSigned(constVal));
    } else {
      parent->children.insert(parent->children.begin(), newConstantNode(constVal));
    }
  } else {
    auto prod = newNode(NodeType::PRODUCT);
    prod->children.push_back(newConstantNode(constVal));
    prod->children.push_back(getShallowCopy());
    copy(*prod);
  }

  return true;
}

// --------------------------------------------------------- resolve product of powers
bool Node::checkResolveProductOfPowers() {
  if (type != NodeType::PRODUCT)
    return false;

  bool changed = false;
  int start = (children[0]->type == NodeType::CONSTANT) ? 1 : 0;

  for (int i = static_cast<int>(children.size()) - 1; i > start; --i) {
    auto child = children[i];
    bool merged = false;

    for (int j = start; j < i; ++j) {
      auto child2 = children[j];

      if (child2->type == NodeType::POWER) {
        auto base2 = child2->children[0];
        auto exp2 = child2->children[1];

        if (base2->equals(*child)) {
          exp2->addConstant(1);
          children.erase(children.begin() + i);
          changed = true;
          merged = true;
          break;
        }

        if (child->type == NodeType::POWER && base2->equals(*child->children[0])) {
          exp2->add(child->children[1]);
          children.erase(children.begin() + i);
          changed = true;
          merged = true;
          break;
        }
      }

      if (child->type == NodeType::POWER) {
        auto base = child->children[0];
        auto exp = child->children[1];

        if (base->equals(*child2)) {
          exp->addConstant(1);
          children[j] = children[i];
          children.erase(children.begin() + i);
          changed = true;
        }
        break;
      }

      if (child->equals(*child2)) {
        children[j] = newNodeWithChildren(NodeType::POWER, {child, newConstantNode(2)});
        children.erase(children.begin() + i);
        changed = true;
        merged = true;
        break;
      }
    }
    (void)merged;
  }

  if (children.size() == 1)
    copy(*children[0]);

  return changed;
}

// --------------------------------------------------------- add
void Node::add(const std::shared_ptr<Node> &other) {
  if (type == NodeType::CONSTANT) {
    MBAValue constant = this->constant;
    copy(*other->getCopy());
    constant = getReducedConstant(constant);
    addConstant(static_cast<int64_t>(MBAOps::toLow64(constant, bitCount)));
    return;
  }

  if (other->type == NodeType::CONSTANT) {
    addConstant(static_cast<int64_t>(MBAOps::toLow64(other->constant, bitCount)));
    return;
  }

  if (type == NodeType::SUM) {
    addToSum(other);
    return;
  }

  if (other->type == NodeType::SUM) {
    auto node = other->getCopy();
    node->addToSum(getCopy());
    copy(*node);
    return;
  }

  auto node = newNodeWithChildren(NodeType::SUM, {getCopy(), other->getCopy()});
  copy(*node);
  mergeSimilarNodesSum();
}

void Node::addConstant(int64_t constant) {
  if (type == NodeType::CONSTANT) {
    this->constant = getReducedConstant(this->constant + MBAOps::fromSigned(constant));
    return;
  }

  if (type == NodeType::SUM) {
    if (!children.empty() && children[0]->type == NodeType::CONSTANT) {
      children[0]->addConstant(constant);
      return;
    }
    children.insert(children.begin(), newConstantNode(constant));
    return;
  }

  auto node = newNodeWithChildren(NodeType::SUM, {newConstantNode(constant), getCopy()});
  copy(*node);
}

void Node::addToSum(const std::shared_ptr<Node> &other) {
  if (other->type == NodeType::SUM) {
    for (auto &ochild : other->children) {
      if (ochild->type == NodeType::CONSTANT)
        addConstant(static_cast<int64_t>(MBAOps::toLow64(ochild->constant, bitCount)));
      else
        children.push_back(ochild->getCopy());
    }
  } else {
    children.push_back(other->getCopy());
  }

  mergeSimilarNodesSum();
}

// --------------------------------------------------------- product of constant and sum
bool Node::checkResolveProductOfConstantAndSum() {
  if (type != NodeType::PRODUCT)
    return false;

  if (children.size() < 2)
    return false;

  auto child0 = children[0];
  if (child0->type != NodeType::CONSTANT)
    return false;

  auto child1 = children[1];
  if (child1->type != NodeType::SUM)
    return false;

  MBAValue constant = child0->constant;
  Node *sumNode = this;
  if (children.size() == 2) {
    copy(*child1);
  } else {
    children.erase(children.begin());
    sumNode = children[0].get();
  }

  for (size_t i = 0; i < sumNode->children.size(); ++i) {
    if (sumNode->children[i]->type == NodeType::CONSTANT) {
      sumNode->children[i]->constant = sumNode->children[i]->getReducedConstant(
          sumNode->children[i]->constant * constant);
    } else if (sumNode->children[i]->type == NodeType::PRODUCT) {
      auto first = sumNode->children[i]->children[0];
      if (first->type == NodeType::CONSTANT) {
        first->constant = first->getReducedConstant(first->constant * constant);
      } else {
        sumNode->children[i]->children.insert(sumNode->children[i]->children.begin(),
                                             sumNode->newConstantNode(static_cast<int64_t>(MBAOps::toLow64(constant, bitCount))));
      }
    } else {
      auto factors = std::vector<std::shared_ptr<Node>>{
          sumNode->newConstantNode(static_cast<int64_t>(MBAOps::toLow64(constant, bitCount))),
          sumNode->children[i]};
      sumNode->children[i] = sumNode->newNodeWithChildren(NodeType::PRODUCT, factors);
    }
  }

  return true;
}

// --------------------------------------------------------- factor out of sum
bool Node::checkFactorOutOfSum() {
  if (type != NodeType::SUM || children.size() <= 1)
    return false;

  std::vector<std::shared_ptr<Node>> factors;
  while (true) {
    auto factor = tryFactorOutOfSum();
    if (factor == nullptr)
      break;
    factors.push_back(factor);
  }

  if (factors.empty())
    return false;

  factors.push_back(getCopy());
  auto prod = newNodeWithChildren(NodeType::PRODUCT, factors);
  copy(*prod);
  return true;
}

std::shared_ptr<Node> Node::tryFactorOutOfSum() {
  auto factor = getCommonFactorInSum();
  if (factor == nullptr)
    return nullptr;

  for (auto &child : children)
    child->eliminateFactor(factor);

  return factor;
}

std::shared_ptr<Node> Node::getCommonFactorInSum() {
  auto first = children[0];

  if (first->type == NodeType::PRODUCT) {
    for (auto &child : first->children) {
      if (child->type == NodeType::CONSTANT)
        continue;
      if (hasFactorInRemainingChildren(child))
        return child->getCopy();

      if (child->type == NodeType::POWER) {
        auto exp = child->children[1];
        if (exp->type == NodeType::CONSTANT && !exp->isConstant(0)) {
          auto base = child->children[0];
          if (hasFactorInRemainingChildren(base))
            return base->getCopy();
        }
      }
    }
    return nullptr;
  }

  if (first->type == NodeType::POWER) {
    auto exp = first->children[1];
    if (exp->type == NodeType::CONSTANT && !exp->isConstant(0)) {
      auto base = first->children[0];
      if (base->type != NodeType::CONSTANT && hasFactorInRemainingChildren(base))
        return base->getCopy();
    }
    return nullptr;
  }

  if (first->type != NodeType::CONSTANT && hasFactorInRemainingChildren(first))
    return first->getCopy();

  return nullptr;
}

bool Node::hasFactorInRemainingChildren(const std::shared_ptr<Node> &factor) {
  for (size_t i = 1; i < children.size(); ++i)
    if (!children[i]->hasFactor(factor))
      return false;
  return true;
}

bool Node::hasFactor(const std::shared_ptr<Node> &factor) {
  if (type == NodeType::PRODUCT)
    return hasFactorProduct(factor);

  if (type == NodeType::POWER) {
    auto exp = children[1];
    if (exp->type == NodeType::CONSTANT && !exp->isConstant(0))
      return children[0]->equals(*factor);
  }

  return equals(*factor);
}

bool Node::hasFactorProduct(const std::shared_ptr<Node> &factor) {
  for (auto &child : children) {
    if (child->equals(*factor))
      return true;
    if (child->type == NodeType::POWER) {
      auto exp = child->children[1];
      if (exp->type == NodeType::CONSTANT && !exp->isConstant(0))
        if (child->children[0]->equals(*factor))
          return true;
    }
  }
  return false;
}

bool Node::hasChild(const Node &node) const {
  for (size_t i = 0; i < children.size(); ++i)
    if (children[i]->equals(node))
      return true;
  return false;
}

int Node::getIndexOfChild(const std::shared_ptr<Node> &node) const {
  for (size_t i = 0; i < children.size(); ++i)
    if (children[i]->equals(*node))
      return static_cast<int>(i);
  return -1;
}

int Node::getIndexOfChildNegated(const std::shared_ptr<Node> &node) const {
  for (size_t i = 0; i < children.size(); ++i)
    if (children[i]->equalsNegated(*node))
      return static_cast<int>(i);
  return -1;
}

void Node::eliminateFactor(const std::shared_ptr<Node> &factor) {
  if (type == NodeType::PRODUCT) {
    eliminateFactorProduct(factor);
    return;
  }

  if (type == NodeType::POWER) {
    eliminateFactorPower(factor);
    return;
  }

  auto c = newConstantNode(1);
  copy(*c);
}

void Node::eliminateFactorProduct(const std::shared_ptr<Node> &factor) {
  for (size_t i = 0; i < children.size(); ++i) {
    auto child = children[i];

    if (child->equals(*factor)) {
      children.erase(children.begin() + i);
      if (children.size() == 1)
        copy(*children[0]);
      return;
    }

    if (child->type == NodeType::POWER && child->children[0]->equals(*factor)) {
      child->decrementExponent();
      return;
    }
  }
}

void Node::eliminateFactorPower(const std::shared_ptr<Node> &factor) {
  if (equals(*factor)) {
    copy(*newConstantNode(1));
    return;
  }

  decrementExponent();
}

void Node::decrementExponent() {
  children[1]->decrement();

  if (children[1]->isConstant(1)) {
    copy(*children[0]);
  } else if (children[1]->isConstant(0)) {
    copy(*newConstantNode(1));
  }
}

void Node::decrement() { addConstant(-1); }

// --------------------------------------------------------- inverse negations in sum
bool Node::checkResolveInverseNegationsInSum() {
  if (type != NodeType::SUM)
    return false;

  bool changed = false;
  int64_t constTerm = 0;

  int i = static_cast<int>(children.size());
  while (i > 1) {
    --i;
    auto first = children[i];

    for (int j = 0; j < i; ++j) {
      auto second = children[j];

      if (first->equalsNegated(*second)) {
        children.erase(children.begin() + i);
        children.erase(children.begin() + j);
        --i;
        constTerm -= 1;
        changed = true;
        break;
      }

      if (first->type != NodeType::PRODUCT)
        continue;
      if (second->type != NodeType::PRODUCT)
        continue;

      int firstIdx = -1, secIdx = -1;
      bool haveIdx = first->getOnlyDifferingChildIndices(*second, &firstIdx, &secIdx);
      if (!haveIdx)
        continue;

      if (first->children[firstIdx]->equalsNegated(*second->children[secIdx])) {
        children.erase(children.begin() + i);
        second->children.erase(second->children.begin() + secIdx);
        if (second->children.size() == 1)
          second->copy(*second->children[0]);
        second->multiplyByMinusOne();
        changed = true;
        break;
      }
    }
  }

  if (!children.empty() && children[0]->type == NodeType::CONSTANT) {
    children[0]->constant = children[0]->getReducedConstant(children[0]->constant + MBAOps::fromSigned(constTerm));
  } else {
    children.insert(children.begin(), newConstantNode(constTerm));
  }
  if (children.size() > 1 && children[0]->isConstant(0))
    children.erase(children.begin());

  if (children.size() == 1) {
    copy(*children[0]);
  } else if (children.empty()) {
    copy(*newConstantNode(0));
  }

  return changed;
}

// --------------------------------------------------------- differing child indices
bool Node::getOnlyDifferingChildIndices(const Node &other, int *firstIdx, int *secIdx) const {
  if (type == other.type) {
    if (children.size() != other.children.size())
      return false;
    return getOnlyDifferingChildIndicesSameLen(other, firstIdx, secIdx);
  }

  if (children.size() == other.children.size()) {
    if (children.size() != 2)
      return false;
    return getOnlyDifferingChildIndicesSameLen(other, firstIdx, secIdx);
  }

  if (children.size() < other.children.size()) {
    if (children.size() != 2)
      return false;
    return getOnlyDifferingChildIndicesDiffLen(other, firstIdx, secIdx);
  }

  if (other.children.size() != 2)
    return false;

  int oi1 = -1, oi2 = -1;
  if (!other.getOnlyDifferingChildIndicesDiffLen(*this, &oi1, &oi2))
    return false;
  if (firstIdx)
    *firstIdx = oi2;
  if (secIdx)
    *secIdx = oi1;
  return true;
}

bool Node::getOnlyDifferingChildIndicesSameLen(const Node &other, int *firstIdx, int *secIdx) const {
  int idx1 = -1;

  std::vector<int> oIndices;
  for (size_t i = 0; i < other.children.size(); ++i)
    oIndices.push_back(static_cast<int>(i));
  for (size_t i = 0; i < children.size(); ++i) {
    auto child = children[i];
    bool found = false;
    for (auto it = oIndices.begin(); it != oIndices.end(); ++it) {
      if (child->equals(*other.children[*it])) {
        oIndices.erase(it);
        found = true;
        break;
      }
    }

    if (!found) {
      if (idx1 == -1)
        idx1 = static_cast<int>(i);
      else
        return false;
    }
  }

  if (idx1 == -1)
    return false;

  if (firstIdx)
    *firstIdx = idx1;
  if (secIdx)
    *secIdx = oIndices[0];
  return true;
}

bool Node::getOnlyDifferingChildIndicesDiffLen(const Node &other, int *firstIdx, int *secIdx) const {
  for (int i : {0, 1}) {
    int idx = other.getIndexOfChildNegated(children[i]);
    if (idx == -1)
      continue;

    int oi = (i == 0) ? 1 : 0;

    if (children[oi]->type != other.type)
      continue;

    std::vector<std::shared_ptr<Node>> rest;
    for (int k = 0; k < static_cast<int>(other.children.size()); ++k)
      if (k != idx)
        rest.push_back(other.children[k]);
    if (!doChildrenMatch(children[oi]->children, rest))
      continue;

    if (firstIdx)
      *firstIdx = i;
    if (secIdx)
      *secIdx = idx;
    return true;
  }

  return false;
}

// Mirrors node.py do_children_match.
bool doChildrenMatch(const std::vector<std::shared_ptr<Node>> &l1,
                     const std::vector<std::shared_ptr<Node>> &l2) {
  if (l1.size() != l2.size())
    return false;
  for (auto &child : l1) {
    bool found = false;
    for (auto &o : l2)
      if (child->equals(*o)) {
        found = true;
        break;
      }
    if (!found)
      return false;
  }
  return true;
}

} // namespace MBA
} // namespace LSiMBA
