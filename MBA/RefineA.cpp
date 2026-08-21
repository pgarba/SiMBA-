// GAMBA native C++ port — Refine batch A (orchestrator + step 1 + helpers).
// Mirrors external/GAMBA/src/utils/node.py.
#include "Node.h"

#include <algorithm>
#include <cstdio>

namespace LSiMBA {
namespace MBA {

// --------------------------------------------------------------- orchestrator
// Mirrors node.py refine / __refine_step_1 / __refine_step_2.
void Node::refine(Node *parent, bool restrictedScope) {
  for (int i = 0; i < MAX_IT; ++i) {
    refineStep1(restrictedScope);
    if (!refineStep2(parent, restrictedScope))
      return;
  }
}

void Node::refineStep1(bool restrictedScope) {
  if (!restrictedScope)
    for (auto &c : children)
      c->refineStep1();

  inspectConstants();
  flatten();
  checkDuplicateChildren();
  resolveInverseNodes();
  removeTrivialNodes();
}

bool Node::refineStep2(Node *parent, bool restrictedScope) {
  bool changed = false;
  if (!restrictedScope)
    for (auto &c : children)
      if (c->refineStep2(this))
        changed = true;

  if (eliminateNestedNegationsAdvanced())          changed = true;
  if (checkBitwiseNegations(parent))               changed = true;
  if (checkBitwisePowersOfTwo())                  changed = true;
  if (checkBeautifyConstantsInProducts())         changed = true;
  if (checkMoveInBitwiseNegations())              changed = true;
  if (checkBitwiseNegationsInExclDisjunctions())  changed = true;
  if (checkRewritePowers(parent))                 changed = true;
  if (checkResolveProductOfPowers())              changed = true;
  if (checkResolveProductOfConstantAndSum())      changed = true;
  if (checkFactorOutOfSum())                      changed = true;
  if (checkResolveInverseNegationsInSum())        changed = true;
  if (insertFixedInConj())                       changed = true;
  if (insertFixedInDisj())                       changed = true;
  if (checkTrivialXor())                         changed = true;
  if (checkXorSameMultByMinusOne())              changed = true;
  if (checkConjZeroRule())                       changed = true;
  if (checkConjNegXorZeroRule())                 changed = true;
  if (checkConjNegXorMinusOneRule())             changed = true;
  if (checkConjNegatedXorZeroRule())             changed = true;
  if (checkConjXorIdentityRule())                changed = true;
  if (checkDisjXorIdentityRule())                changed = true;
  if (checkConjNegConjIdentityRule())            changed = true;
  if (checkDisjDisjIdentityRule())               changed = true;
  if (checkConjConjIdentityRule())               changed = true;
  if (checkDisjConjIdentityRule())               changed = true;
  if (checkDisjConjIdentityRule2())              changed = true;
  if (checkConjDisjIdentityRule())               changed = true;
  if (checkDisjNegDisjIdentityRule())            changed = true;
  if (checkDisjSubDisjIdentityRule())            changed = true;
  if (checkDisjSubConjIdentityRule())            changed = true;
  if (checkConjAddConjIdentityRule())            changed = true;
  if (checkDisjDisjConjRule())                   changed = true;
  if (checkConjConjDisjRule())                   changed = true;
  if (checkDisjDisjConjRule2())                  changed = true;

  return changed;
}

// --------------------------------------------------------------- inspect constants
void Node::inspectConstants() {
  if (type == NodeType::INCL_DISJUNCTION)
    inspectConstantsInclDisjunction();
  else if (type == NodeType::EXCL_DISJUNCTION)
    inspectConstantsExclDisjunction();
  else if (type == NodeType::CONJUNCTION)
    inspectConstantsConjunction();
  else if (type == NodeType::SUM)
    inspectConstantsSum();
  else if (type == NodeType::PRODUCT)
    inspectConstantsProduct();
  else if (type == NodeType::NEGATION)
    inspectConstantsNegation();
  else if (type == NodeType::POWER)
    inspectConstantsPower();
  else if (type == NodeType::CONSTANT)
    inspectConstantsConstant();
}

void Node::inspectConstantsInclDisjunction() {
  auto first = children[0];
  bool isMinusOne = first->isConstant(-1);
  std::vector<std::shared_ptr<Node>> toRemove;

  if (!isMinusOne) {
    for (size_t i = 1; i < children.size(); ++i) {
      auto child = children[i];
      if (child->type == NodeType::CONSTANT) {
        if (child->constant == MBAValue(128, 0)) {
          toRemove.push_back(child);
          continue;
        }
        if (child->isConstant(-1)) {
          isMinusOne = true;
          break;
        }
        first = children[0];
        if (first->type == NodeType::CONSTANT) {
          first->constant = first->getReducedConstant(first->constant | child->constant);
          toRemove.push_back(child);
        } else {
          children.erase(std::find(children.begin(), children.end(), child));
          children.insert(children.begin(), child);
        }
      }
    }
  }

  if (isMinusOne) {
    children.clear();
    type = NodeType::CONSTANT;
    constant = MBAOps::fromSigned(-1);
    reduceConstant();
    return;
  }

  for (auto child : toRemove)
    children.erase(std::find(children.begin(), children.end(), child));

  first = children[0];
  if (children.size() > 1 && first->isConstant(0))
    children.erase(children.begin());
  if (children.size() == 1)
    copy(*children[0]);
}

void Node::inspectConstantsExclDisjunction() {
  std::vector<std::shared_ptr<Node>> toRemove;

  for (size_t i = 1; i < children.size(); ++i) {
    auto child = children[i];
    if (child->type == NodeType::CONSTANT) {
      if (child->constant == MBAValue(128, 0)) {
        toRemove.push_back(child);
        continue;
      }
      auto first = children[0];
      if (first->type == NodeType::CONSTANT) {
        first->constant = first->getReducedConstant(first->constant ^ child->constant);
        toRemove.push_back(child);
      } else {
        children.erase(std::find(children.begin(), children.end(), child));
        children.insert(children.begin(), child);
      }
    }
  }

  for (auto child : toRemove)
    children.erase(std::find(children.begin(), children.end(), child));

  auto first = children[0];
  if (children.size() > 1 && first->isConstant(0))
    children.erase(children.begin());
  if (children.size() == 1)
    copy(*children[0]);
}

void Node::inspectConstantsConjunction() {
  auto first = children[0];
  bool isZero = first->isConstant(0);
  std::vector<std::shared_ptr<Node>> toRemove;

  if (!isZero) {
    for (size_t i = 1; i < children.size(); ++i) {
      auto child = children[i];
      if (child->type == NodeType::CONSTANT) {
        if (child->isConstant(-1)) {
          toRemove.push_back(child);
          continue;
        }
        if (child->constant == MBAValue(128, 0)) {
          isZero = true;
          break;
        }
        first = children[0];
        if (first->type == NodeType::CONSTANT) {
          first->constant = first->getReducedConstant(first->constant & child->constant);
          toRemove.push_back(child);
        } else {
          children.erase(std::find(children.begin(), children.end(), child));
          children.insert(children.begin(), child);
        }
      }
    }
  }

  if (isZero) {
    children.clear();
    type = NodeType::CONSTANT;
    constant = MBAValue(128, 0);
    return;
  }

  for (auto child : toRemove)
    children.erase(std::find(children.begin(), children.end(), child));

  first = children[0];
  if (children.size() > 1 && first->isConstant(-1))
    children.erase(children.begin());
  if (children.size() == 1)
    copy(*children[0]);
}

void Node::inspectConstantsSum() {
  auto first = children[0];
  std::vector<std::shared_ptr<Node>> toRemove;

  for (size_t i = 1; i < children.size(); ++i) {
    auto child = children[i];
    if (child->type == NodeType::CONSTANT) {
      if (child->constant == MBAValue(128, 0)) {
        toRemove.push_back(child);
        continue;
      }
      first = children[0];
      if (first->type == NodeType::CONSTANT) {
        first->constant = first->getReducedConstant(first->constant + child->constant);
        toRemove.push_back(child);
      } else {
        children.erase(std::find(children.begin(), children.end(), child));
        children.insert(children.begin(), child);
      }
    }
  }

  for (auto child : toRemove)
    children.erase(std::find(children.begin(), children.end(), child));

  first = children[0];
  if (children.size() > 1 && first->isConstant(0))
    children.erase(children.begin());
  if (children.size() == 1)
    copy(*children[0]);
}

void Node::inspectConstantsProduct() {
  auto first = children[0];
  bool isZero = first->isConstant(0);
  std::vector<std::shared_ptr<Node>> toRemove;

  if (!isZero) {
    for (size_t i = 1; i < children.size(); ++i) {
      auto child = children[i];
      if (child->type == NodeType::CONSTANT) {
        if (child->constant == MBAValue(128, 1)) {
          toRemove.push_back(child);
          continue;
        }
        if (child->constant == MBAValue(128, 0)) {
          isZero = true;
          break;
        }
        first = children[0];
        if (first->type == NodeType::CONSTANT) {
          first->constant = first->getReducedConstant(first->constant * child->constant);
          toRemove.push_back(child);
        } else {
          children.erase(std::find(children.begin(), children.end(), child));
          children.insert(children.begin(), child);
        }
      }
    }
  }

  if (isZero) {
    children.clear();
    type = NodeType::CONSTANT;
    constant = MBAValue(128, 0);
    return;
  }

  for (auto child : toRemove)
    children.erase(std::find(children.begin(), children.end(), child));

  first = children[0];
  if (children.size() > 1 && first->isConstant(1))
    children.erase(children.begin());
  if (children.size() == 1)
    copy(*children[0]);
}

void Node::inspectConstantsNegation() {
  auto child = children[0];
  if (child->type == NodeType::NEGATION) {
    copy(*child->children[0]);
  } else if (child->type == NodeType::CONSTANT) {
    child->constant = child->getReducedConstant(-child->constant - MBAValue(128, 1));
    copy(*child);
  }
}

void Node::inspectConstantsPower() {
  auto base = children[0];
  auto exp = children[1];

  if (base->type == NodeType::CONSTANT && exp->type == NodeType::CONSTANT) {
    uint64_t b = MBAOps::toLow64(base->constant, bitCount);
    uint64_t e = MBAOps::toLow64(exp->constant, bitCount);
    base->constant = base->getReducedConstant(MBAOps::fromSigned(
        static_cast<int64_t>(MBAOps::power(b, e, bitCount))));
    copy(*base);
    return;
  }

  if (exp->type == NodeType::CONSTANT) {
    if (exp->constant == MBAValue(128, 0)) {
      type = NodeType::CONSTANT;
      constant = MBAValue(128, 1);
      children.clear();
    } else if (exp->constant == MBAValue(128, 1)) {
      copy(*base);
    }
  }
}

void Node::inspectConstantsConstant() { reduceConstant(); }

// --------------------------------------------------------------- flatten
void Node::flatten() {
  if (type == NodeType::INCL_DISJUNCTION || type == NodeType::EXCL_DISJUNCTION ||
      type == NodeType::CONJUNCTION || type == NodeType::SUM)
    flattenBinaryGeneric();
  else if (type == NodeType::PRODUCT)
    flattenProduct();
}

void Node::flattenBinaryGeneric() {
  bool changed = false;
  for (int i = static_cast<int>(children.size()) - 1; i >= 0; --i) {
    auto child = children[i];
    if (child->type != type)
      continue;
    children.erase(children.begin() + i);
    for (auto &c : child->children)
      children.push_back(c);
    changed = true;
  }
  if (changed)
    inspectConstants();
}

void Node::flattenProduct() {
  bool changed = false;
  size_t i = 0;

  while (i < children.size()) {
    auto child = children[i];
    if (child->type != type) {
      ++i;
      continue;
    }

    changed = true;
    children.erase(children.begin() + i);
    if (child->children[0]->type == NodeType::CONSTANT) {
      if (i > 0 && children[0]->type == NodeType::CONSTANT) {
        children[0]->constant = children[0]->getReducedConstant(
            children[0]->constant * child->children[0]->constant);
      } else {
        children.insert(children.begin(), child->children[0]);
        ++i;
      }
      child->children.erase(child->children.begin());
    }

    for (int k = static_cast<int>(child->children.size()) - 1; k >= 0; --k)
      children.insert(children.begin() + i + k, child->children[k]);
    i += child->children.size();
  }

  if (!changed)
    return;

  if (children.size() > 1) {
    auto first = children[0];
    if (first->type != NodeType::CONSTANT)
      return;
    if (!first->isConstant(1))
      return;
    children.erase(children.begin());
  }

  if (children.size() == 1)
    copy(*children[0]);
}

// --------------------------------------------------------------- duplicate children
void Node::checkDuplicateChildren() {
  if (type == NodeType::INCL_DISJUNCTION || type == NodeType::CONJUNCTION)
    removeDuplicateChildren();
  else if (type == NodeType::EXCL_DISJUNCTION)
    removePairsOfChildren();
  else if (type == NodeType::SUM)
    mergeSimilarNodesSum();
}

void Node::removeDuplicateChildren() {
  size_t i = 0;
  while (i < children.size()) {
    for (int j = static_cast<int>(children.size()) - 1; j > static_cast<int>(i); --j) {
      if (children[i]->equals(*children[j]))
        children.erase(children.begin() + j);
    }
    ++i;
  }
}

void Node::removePairsOfChildren() {
  size_t i = 0;
  while (i < children.size()) {
    bool removed = false;
    for (int j = static_cast<int>(children.size()) - 1; j > static_cast<int>(i); --j) {
      if (children[i]->equals(*children[j])) {
        children.erase(children.begin() + j);
        children.erase(children.begin() + i);
        if (i > 0)
          --i;
        removed = true;
        break;
      }
    }
    if (!removed)
      ++i;
  }

  if (children.empty())
    children.push_back(newConstantNode(0));
}

void Node::mergeSimilarNodesSum() {
  size_t i = 0;
  while (i + 1 < children.size()) {
    size_t j = i + 1;
    while (j < children.size()) {
      if (tryMergeSumChildren(static_cast<int>(i), static_cast<int>(j)))
        children.erase(children.begin() + j);
      else
        ++j;
    }

    if (children[i]->isZeroProduct()) {
      children.erase(children.begin() + i);
    } else {
      if (children[i]->hasFactorOne()) {
        children[i]->children.erase(children[i]->children.begin());
        if (children[i]->children.size() == 1)
          children[i] = children[i]->children[0];
      }
      ++i;
    }
  }

  if (children.size() > 1)
    return;

  if (children.size() == 1) {
    copy(*children[0]);
    return;
  }

  type = NodeType::CONSTANT;
  children.clear();
  constant = MBAValue(128, 0);
}

bool Node::isZeroProduct() const {
  return type == NodeType::PRODUCT && children[0]->isConstant(0);
}

bool Node::hasFactorOne() const {
  return type == NodeType::PRODUCT && children[0]->isConstant(1);
}

bool Node::getOptConstFactor(MBAValue &out) const {
  if (type == NodeType::PRODUCT && children[0]->type == NodeType::CONSTANT) {
    out = children[0]->constant;
    return true;
  }
  return false;
}

bool Node::tryMergeSumChildren(int i, int j) {
  auto child1 = children[i];
  MBAValue const1, const2;
  bool has1 = child1->getOptConstFactor(const1);

  auto child2 = children[j];
  bool has2 = child2->getOptConstFactor(const2);

  if (!equalsNeglectingConstants(*child2, has1, has2))
    return false;

  if (!has2)
    const2 = MBAValue(128, 1);

  if (!has1) {
    if (child1->type == NodeType::PRODUCT) {
      child1->children.insert(child1->children.begin(),
                             newConstantNode(static_cast<int64_t>(MBAOps::toLow64(MBAValue(128, 1) + const2, bitCount))));
    } else {
      auto c = newConstantNode(static_cast<int64_t>(MBAOps::toLow64(MBAValue(128, 1) + const2, bitCount)));
      children[i] = newNodeWithChildren(NodeType::PRODUCT, {c, child1});
    }
  } else {
    child1->children[0]->constant += const2;
    child1->children[0]->reduceConstant();
  }

  return true;
}

bool Node::equalsNeglectingConstants(const Node &other, bool hasConst, bool hasConstOther) const {
  if (hasConst) {
    if (hasConstOther)
      return equalsNeglectingConstantsBothConst(other);
    return equalsNeglectingConstantsOtherConst(other);
  }

  if (hasConstOther)
    return equalsNeglectingConstantsOtherConst(other);
  return equals(other);
}

bool Node::equalsNeglectingConstantsOtherConst(const Node &other) const {
  if (other.children.size() == 2)
    return equals(*other.children[1]);
  if (type != NodeType::PRODUCT)
    return false;
  if (children.size() != other.children.size() - 1)
    return false;

  std::vector<size_t> oIndices;
  for (size_t i = 1; i < other.children.size(); ++i)
    oIndices.push_back(i);
  for (auto &child : children) {
    bool found = false;
    for (auto it = oIndices.begin(); it != oIndices.end(); ++it) {
      if (child->equals(*other.children[*it])) {
        oIndices.erase(it);
        found = true;
        break;
      }
    }
    if (!found)
      return false;
  }

  return true;
}

bool Node::equalsNeglectingConstantsBothConst(const Node &other) const {
  if (children.size() != other.children.size())
    return false;
  if (children.size() == 2)
    return children[1]->equals(*other.children[1]);

  std::vector<size_t> oIndices;
  for (size_t i = 1; i < other.children.size(); ++i)
    oIndices.push_back(i);
  for (size_t i = 1; i < children.size(); ++i) {
    auto child = children[i];
    bool found = false;
    for (auto it = oIndices.begin(); it != oIndices.end(); ++it) {
      if (child->equals(*other.children[*it])) {
        oIndices.erase(it);
        found = true;
        break;
      }
    }
    if (!found)
      return false;
  }

  return true;
}

// --------------------------------------------------------------- inverse nodes
void Node::resolveInverseNodes() {
  if (type == NodeType::INCL_DISJUNCTION || type == NodeType::EXCL_DISJUNCTION ||
      type == NodeType::CONJUNCTION)
    resolveInverseNodesBitwise();
}

void Node::resolveInverseNodesBitwise() {
  size_t i = 0;
  while (true) {
    if (i >= children.size())
      break;

    auto child1 = children[i];

    for (size_t j = i + 1; j < children.size(); ++j) {
      auto child2 = children[j];

      if (!child1->isBitwiseInverse(*child2))
        continue;

      if (type != NodeType::EXCL_DISJUNCTION || children.size() == 2) {
        copy(*newConstantNode(type == NodeType::CONJUNCTION ? 0 : -1));
        return;
      }

      children.erase(children.begin() + j);
      children.erase(children.begin() + i);

      if (children[0]->type == NodeType::CONSTANT) {
        children[0]->constant = children[0]->getReducedConstant(
            MBAValue(128, -1) ^ children[0]->constant);
        if (i > 0)
          --i;
      } else {
        children.insert(children.begin(), newConstantNode(-1));
      }

      if (children.size() == 1) {
        copy(*children[0]);
        return;
      }

      break;
    }

    ++i;
  }
}

bool Node::isBitwiseInverse(const Node &other) const {
  if (type == NodeType::NEGATION) {
    if (other.type == NodeType::NEGATION)
      return children[0]->isBitwiseInverse(*other.children[0]);
    return children[0]->equals(other);
  }

  if (other.type == NodeType::NEGATION)
    return equals(*other.children[0]);

  auto node = getCopy();
  if (node->type == NodeType::PRODUCT && node->children.size() == 2 &&
      node->children[0]->type == NodeType::CONSTANT) {
    if (node->children[1]->type == NodeType::SUM) {
      int64_t fac = static_cast<int64_t>(MBAOps::toLow64(node->children[0]->constant, bitCount));
      for (auto &n : node->children[1]->children)
        n->multiply(fac);
      node->copy(*node->children[1]);
    }
  }

  auto onode = other.getCopy();
  if (onode->type == NodeType::PRODUCT && onode->children.size() == 2 &&
      onode->children[0]->type == NodeType::CONSTANT) {
    if (onode->children[1]->type == NodeType::SUM) {
      int64_t fac = static_cast<int64_t>(MBAOps::toLow64(onode->children[0]->constant, bitCount));
      for (auto &n : onode->children[1]->children)
        n->multiply(fac);
      onode->copy(*onode->children[1]);
    }
  }

  if (node->type == NodeType::SUM) {
    if (onode->type != NodeType::SUM) {
      if (node->children.size() > 2 || !node->children[0]->isConstant(-1))
        return false;
      node->children[1]->multiplyByMinusOne();
      return node->children[1]->equals(*onode);
    }

    for (auto &child : node->children)
      child->multiplyByMinusOne();
    if (node->children[0]->type == NodeType::CONSTANT) {
      node->children[0]->constant -= MBAValue(128, 1);
      node->children[0]->reduceConstant();
      if (node->children[0]->isConstant(0))
        node->children.erase(node->children.begin());
      if (children.size() == 1)
        return false;
    } else {
      node->children.insert(node->children.begin(), newConstantNode(-1));
    }

    return node->equals(*onode);
  }

  if (onode->type != NodeType::SUM)
    return false;

  if (onode->children.size() > 2 || !onode->children[0]->isConstant(-1))
    return false;

  onode->children[1]->multiplyByMinusOne();
  return onode->children[1]->equals(*node);
}

// --------------------------------------------------------------- trivial nodes
void Node::removeTrivialNodes() {
  if (typeRank(type) < typeRank(NodeType::PRODUCT))
    return;
  if (children.size() == 1)
    copy(*children[0]);
}

// --------------------------------------------------------------- nested negations
bool Node::eliminateNestedNegationsAdvanced() {
  if (type == NodeType::NEGATION) {
    auto child = children[0];
    if (child->type == NodeType::NEGATION) {
      copy(*child->children[0]);
      return true;
    }

    auto node = child->getOptTransformedNegated();
    if (node != nullptr) {
      copy(*node);
      return true;
    }

    if (child->type == NodeType::SUM && child->children[0]->isConstant(-1)) {
      type = NodeType::SUM;
      children.assign(child->children.begin() + 1, child->children.end());
      for (auto &c : children)
        c->multiplyByMinusOne();
      if (children.size() == 1)
        copy(*children[0]);
      return true;
    }

    return false;
  }

  auto child = getOptTransformedNegated();
  if (child == nullptr)
    return false;

  if (child->type == NodeType::NEGATION) {
    copy(*child->children[0]);
    return true;
  }

  auto node = child->getOptTransformedNegated();
  if (node != nullptr) {
    copy(*node);
    return true;
  }

  return false;
}

} // namespace MBA
} // namespace LSiMBA
