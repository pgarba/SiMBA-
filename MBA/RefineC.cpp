// GAMBA native C++ port — Refine batch C (conjunction/disjunction identity rules).
// Mirrors external/GAMBA/src/utils/node.py.
#include "Node.h"

#include <algorithm>

namespace LSiMBA {
namespace MBA {

// --------------------------------------------------------- fixed true/false
bool Node::insertFixedInConj() {
  if (type != NodeType::CONJUNCTION)
    return false;

  bool changed = false;
  for (size_t i = 0; i < children.size(); ++i) {
    auto child1 = children[i];
    for (size_t j = 0; j < children.size(); ++j) {
      if (i == j)
        continue;
      if (children[j]->checkInsertFixedTrue(child1))
        changed = true;
    }
  }
  return changed;
}

bool Node::checkInsertFixedTrue(const std::shared_ptr<Node> &node) {
  if (!isBitwiseOp())
    return false;

  bool changed = false;
  for (auto &child : children) {
    if (child->equals(*node)) {
      child->copy(*newConstantNode(-1));
      changed = true;
    } else if (child->isBitwiseOp()) {
      if (child->checkInsertFixedTrue(node))
        changed = true;
    }
  }
  return changed;
}

bool Node::insertFixedInDisj() {
  if (type != NodeType::INCL_DISJUNCTION)
    return false;

  bool changed = false;
  for (size_t i = 0; i < children.size(); ++i) {
    auto child1 = children[i];
    for (size_t j = 0; j < children.size(); ++j) {
      if (i == j)
        continue;
      if (children[j]->checkInsertFixedFalse(child1))
        changed = true;
    }
  }
  return changed;
}

bool Node::checkInsertFixedFalse(const std::shared_ptr<Node> &node) {
  if (!isBitwiseOp())
    return false;

  bool changed = false;
  for (auto &child : children) {
    if (child->equals(*node)) {
      child->copy(*newConstantNode(0));
      changed = true;
    } else if (child->isBitwiseOp()) {
      if (child->checkInsertFixedFalse(node))
        changed = true;
    }
  }
  return changed;
}

// --------------------------------------------------------- trivial xor / xor-same
bool Node::checkTrivialXor() {
  if (type != NodeType::EXCL_DISJUNCTION)
    return false;

  auto c = children[0];
  if (children.size() == 2) {
    if (c->isConstant(-1)) {
      type = NodeType::NEGATION;
      children.erase(children.begin());
      return true;
    }
    if (c->isConstant(0)) {
      copy(*children[1]);
      return true;
    }
  } else {
    if (c->isConstant(0)) {
      children.erase(children.begin());
      return true;
    }
  }

  return false;
}

bool Node::checkXorSameMultByMinusOne() {
  if (type != NodeType::PRODUCT)
    return false;

  auto first = children[0];
  if (first->type != NodeType::CONSTANT)
    return false;

  if (MBAOps::toLow64(first->constant, bitCount) % 2 != 0)
    return false;

  bool changed = false;
  for (int i = static_cast<int>(children.size()) - 1; i > 0; --i) {
    auto child = children[i];
    if (child->type != NodeType::CONJUNCTION && child->type != NodeType::INCL_DISJUNCTION)
      continue;

    if (child->children.size() != 2)
      continue;

    auto node = child->children[0]->getCopy();
    node->multiplyByMinusOne();

    if (!node->equals(*child->children[1]))
      continue;

    first->constant = first->constant.lshr(1);
    if (child->type == NodeType::CONJUNCTION)
      first->constant = first->getReducedConstant(-first->constant);

    child->type = NodeType::EXCL_DISJUNCTION;
    changed = true;
    if (MBAOps::toLow64(first->constant, bitCount) % 2 != 0)
      break;
  }

  return changed;
}

// --------------------------------------------------------- conj zero / neg-xor
bool Node::checkConjZeroRule() {
  if (type != NodeType::CONJUNCTION)
    return false;
  if (!hasConjZeroRule())
    return false;

  copy(*newConstantNode(0));
  return true;
}

bool Node::hasConjZeroRule() {
  for (size_t i = 0; i + 1 < children.size(); ++i) {
    auto child1 = children[i];

    for (size_t j = i + 1; j < children.size(); ++j) {
      auto child2 = children[j];
      auto neg2 = child2->getCopy();
      neg2->multiplyByMinusOne();

      if (!child1->equals(*neg2))
        continue;

      auto double1 = child1->getCopy();
      double1->multiply(2);
      auto double2 = child2->getCopy();
      double2->multiply(2);

      for (size_t k = 0; k < children.size(); ++k) {
        if (k == i || k == j)
          continue;

        auto child3 = children[k];
        if (child3->equals(*double1) || child3->equals(*double2))
          return true;
      }
    }
  }

  return false;
}

bool Node::checkConjNegXorZeroRule() {
  if (type != NodeType::CONJUNCTION)
    return false;
  if (!hasConjNegXorZeroRule())
    return false;

  copy(*newConstantNode(0));
  return true;
}

bool Node::hasConjNegXorZeroRule() {
  for (size_t i = 0; i < children.size(); ++i) {
    auto child1 = children[i];

    auto node = child1->getOptArgNegXorSameNeg();
    if (node == nullptr)
      continue;

    auto node2 = node->getCopy();
    node2->multiplyByMinusOne();

    for (size_t j = 0; j < children.size(); ++j) {
      if (i == j)
        continue;

      auto neg2 = children[j]->getCopy();
      neg2->negate();

      if (neg2->isDouble(node) || neg2->isDouble(node2))
        return true;
    }
  }

  return false;
}

std::shared_ptr<Node> Node::getOptArgNegXorSameNeg() {
  if (type != NodeType::PRODUCT)
    return nullptr;
  if (children.size() != 2)
    return nullptr;
  if (!children[0]->isConstant(-1))
    return nullptr;

  auto xnode = children[1];
  if (xnode->type != NodeType::EXCL_DISJUNCTION)
    return nullptr;
  if (xnode->children.size() != 2)
    return nullptr;

  auto node0 = xnode->children[0]->getCopy();
  node0->multiplyByMinusOne();
  if (node0->equals(*xnode->children[1]))
    return node0;

  return nullptr;
}

bool Node::checkConjNegXorMinusOneRule() {
  if (type != NodeType::INCL_DISJUNCTION)
    return false;
  if (!hasDisjNegXorMinusOneRule())
    return false;

  copy(*newConstantNode(-1));
  return true;
}

bool Node::hasDisjNegXorMinusOneRule() {
  for (size_t i = 0; i < children.size(); ++i) {
    auto child1 = children[i];

    if (child1->type != NodeType::NEGATION)
      continue;

    auto node = child1->children[0]->getOptArgNegXorSameNeg();
    if (node == nullptr)
      continue;

    auto node2 = node->getCopy();
    node2->multiplyByMinusOne();

    for (size_t j = 0; j < children.size(); ++j) {
      if (i == j)
        continue;

      auto child2 = children[j];
      if (child2->isDouble(node) || child2->isDouble(node2))
        return true;
    }
  }

  return false;
}

bool Node::checkConjNegatedXorZeroRule() {
  if (type != NodeType::CONJUNCTION)
    return false;
  if (!hasConjNegatedXorZeroRule())
    return false;

  copy(*newConstantNode(0));
  return true;
}

bool Node::hasConjNegatedXorZeroRule() {
  for (size_t i = 0; i < children.size(); ++i) {
    auto child1 = children[i];

    auto node = child1->getOptArgNegatedXorSameNeg();
    if (node == nullptr)
      continue;

    auto node2 = node->getCopy();
    node2->multiplyByMinusOne();

    for (size_t j = 0; j < children.size(); ++j) {
      if (i == j)
        continue;

      auto child2 = children[j]->getCopy();
      if (child2->isDouble(node) || child2->isDouble(node2))
        return true;
    }
  }

  return false;
}

std::shared_ptr<Node> Node::getOptArgNegatedXorSameNeg() {
  if (type != NodeType::NEGATION)
    return nullptr;

  auto xnode = children[0];
  if (xnode->type != NodeType::EXCL_DISJUNCTION)
    return nullptr;
  if (xnode->children.size() != 2)
    return nullptr;

  auto node0 = xnode->children[0]->getCopy();
  node0->multiplyByMinusOne();
  if (node0->equals(*xnode->children[1]))
    return node0;

  return nullptr;
}

// --------------------------------------------------------- xor identity
bool Node::checkConjXorIdentityRule() {
  if (type != NodeType::CONJUNCTION)
    return false;

  bool changed = false;

  int i = -1;
  while (true) {
    ++i;
    if (i >= static_cast<int>(children.size()))
      break;

    auto child1 = children[i];
    if (!child1->isXorSameNeg())
      continue;

    for (int j = 0; j < static_cast<int>(children.size()); ++j) {
      if (j == i)
        continue;

      auto child2 = children[j];

      if (child2->isDouble(child1->children[0]) || child2->isDouble(child1->children[1])) {
        children.erase(children.begin() + i);
        changed = true;
        --i;
        break;
      }
    }
  }

  if (children.size() == 1)
    copy(*children[0]);
  return changed;
}

bool Node::isXorSameNeg() {
  if (type != NodeType::EXCL_DISJUNCTION)
    return false;
  if (children.size() != 2)
    return false;

  auto neg = children[1]->getCopy();
  neg->multiplyByMinusOne();

  return neg->equals(*children[0]);
}

bool Node::isDouble(const std::shared_ptr<Node> &node) {
  auto cpy = node->getCopy();
  cpy->multiply(2);
  return equals(*cpy);
}

bool Node::checkDisjXorIdentityRule() {
  if (type != NodeType::INCL_DISJUNCTION)
    return false;

  bool changed = false;

  int i = -1;
  while (true) {
    ++i;
    if (i >= static_cast<int>(children.size()))
      break;

    auto child1 = children[i];
    auto node = child1->getOptXorDisjXorIdentity();
    if (node == nullptr)
      continue;

    for (int j = 0; j < static_cast<int>(children.size()); ++j) {
      if (j == i)
        continue;

      auto child2 = children[j];

      if (child2->isDouble(node->children[0]) || child2->isDouble(node->children[1])) {
        children.erase(children.begin() + i);
        changed = true;
        --i;
        break;
      }
    }
  }

  if (children.size() == 1)
    copy(*children[0]);
  return changed;
}

std::shared_ptr<Node> Node::getOptXorDisjXorIdentity() {
  if (type != NodeType::PRODUCT)
    return nullptr;
  if (children.size() != 2)
    return nullptr;
  if (!children[0]->isConstant(-1))
    return nullptr;

  auto child = children[1];

  if (child->type != NodeType::EXCL_DISJUNCTION)
    return nullptr;
  if (child->children.size() != 2)
    return nullptr;

  auto neg = child->children[1]->getCopy();
  neg->multiplyByMinusOne();

  return neg->equals(*child->children[0]) ? child : nullptr;
}

// --------------------------------------------------------- neg-conj identity
bool Node::checkConjNegConjIdentityRule() {
  if (type != NodeType::CONJUNCTION)
    return false;

  bool changed = false;

  int i = -1;
  while (true) {
    ++i;
    if (i >= static_cast<int>(children.size()))
      break;

    auto child1 = children[i];
    auto node = child1->getOptArgNegConjDouble();
    if (node == nullptr)
      continue;

    for (int j = 0; j < static_cast<int>(children.size()); ++j) {
      if (j == i)
        continue;

      auto child2 = children[j];
      auto neg = child2->getCopy();
      neg->multiplyByMinusOne();

      if (neg->equals(*node)) {
        children.erase(children.begin() + i);
        changed = true;
        --i;
        break;
      }
    }
  }

  if (children.size() == 1)
    copy(*children[0]);
  return changed;
}

std::shared_ptr<Node> Node::getOptArgNegConjDouble() {
  auto node = getOptArgNegConjDouble1();
  return node != nullptr ? node : getOptArgNegConjDouble2();
}

std::shared_ptr<Node> Node::getOptArgNegConjDouble1() {
  if (type != NodeType::NEGATION)
    return nullptr;

  auto child = children[0];
  if (child->type != NodeType::CONJUNCTION)
    return nullptr;
  if (child->children.size() != 2)
    return nullptr;

  if (child->children[0]->isDouble(child->children[1]))
    return child->children[1];
  if (child->children[1]->isDouble(child->children[0]))
    return child->children[0];

  auto node = child->children[0]->getCopy();
  node->multiplyByMinusOne();
  if (node->isDouble(child->children[1]))
    return child->children[1];

  node = child->children[1]->getCopy();
  node->multiplyByMinusOne();
  if (node->isDouble(child->children[0]))
    return child->children[0];

  return nullptr;
}

std::shared_ptr<Node> Node::getOptArgNegConjDouble2() {
  if (type != NodeType::INCL_DISJUNCTION)
    return nullptr;
  if (children.size() != 2)
    return nullptr;

  auto node0 = children[0]->getCopy();
  node0->negate();
  auto node1 = children[1]->getCopy();
  node1->negate();

  if (node0->isDouble(node1))
    return node1;
  if (node1->isDouble(node0))
    return node0;

  auto node = node0->getCopy();
  node->multiplyByMinusOne();
  if (node->isDouble(node1))
    return node1;

  node1->multiplyByMinusOne();
  if (node1->isDouble(node0))
    return node0;

  return nullptr;
}

// --------------------------------------------------------- nested bitwise identity
bool Node::checkDisjDisjIdentityRule() { return checkNestedBitwiseIdentityRule(NodeType::INCL_DISJUNCTION); }

bool Node::checkConjConjIdentityRule() { return checkNestedBitwiseIdentityRule(NodeType::CONJUNCTION); }

bool Node::checkNestedBitwiseIdentityRule(NodeType t) {
  if (type != t)
    return false;

  bool changed = false;

  int i = -1;
  while (true) {
    ++i;
    if (i >= static_cast<int>(children.size()))
      break;

    auto child1 = children[i];
    auto nodes = child1->getCandidatesNestedBitwiseIdentity(t);

    if (nodes.empty())
      continue;

    for (int j = 0; j < static_cast<int>(children.size()); ++j) {
      if (j == i)
        continue;

      auto child2 = children[j];

      bool done = false;
      for (auto &node : nodes) {
        if (child2->equals(*node)) {
          children.erase(children.begin() + i);
          changed = true;
          done = true;
          --i;
          break;
        }
      }

      if (done)
        break;
    }
  }

  if (children.size() == 1)
    copy(*children[0]);
  return changed;
}

std::vector<std::shared_ptr<Node>> Node::getCandidatesNestedBitwiseIdentity(NodeType t) {
  if (type != NodeType::PRODUCT)
    return {};
  if (children.size() != 2)
    return {};
  if (!children[0]->isConstant(-1))
    return {};

  auto bitw = children[1];
  if (bitw->type != t)
    return {};
  if (bitw->children.size() != 2)
    return {};

  auto neg = bitw->children[1]->getCopy();
  neg->multiplyByMinusOne();

  if (neg->equals(*bitw->children[0]))
    return {bitw->children[0], bitw->children[1]};

  NodeType ot = (t == NodeType::INCL_DISJUNCTION) ? NodeType::CONJUNCTION : NodeType::INCL_DISJUNCTION;

  if (bitw->children[0]->type == ot) {
    if (bitw->children[0]->hasChild(*neg))
      return {neg};
  }

  if (bitw->children[1]->type == ot) {
    neg = bitw->children[0]->getCopy();
    neg->multiplyByMinusOne();

    if (bitw->children[1]->hasChild(*neg))
      return {neg};
  }

  return neg->equals(*bitw->children[0]) ? std::vector<std::shared_ptr<Node>>{bitw} : std::vector<std::shared_ptr<Node>>{};
}

// --------------------------------------------------------- disj-conj identity
bool Node::checkDisjConjIdentityRule() {
  if (type != NodeType::INCL_DISJUNCTION)
    return false;

  bool changed = false;

  int i = -1;
  while (true) {
    ++i;
    if (i >= static_cast<int>(children.size()))
      break;

    auto child1 = children[i];
    auto node = child1->getOptArgDisjConjIdentity();
    if (node == nullptr)
      continue;

    for (int j = 0; j < static_cast<int>(children.size()); ++j) {
      if (j == i)
        continue;

      auto child2 = children[j];
      auto neg = child2->getCopy();
      neg->multiplyByMinusOne();

      if (neg->equals(*node)) {
        children.erase(children.begin() + i);
        changed = true;
        --i;
        break;
      }
    }
  }

  if (children.size() == 1)
    copy(*children[0]);
  return changed;
}

std::shared_ptr<Node> Node::getOptArgDisjConjIdentity() {
  auto node = getOptArgDisjConjIdentity1();
  return node != nullptr ? node : getOptArgDisjConjIdentity2();
}

std::shared_ptr<Node> Node::getOptArgDisjConjIdentity1() {
  if (type != NodeType::CONJUNCTION)
    return nullptr;
  if (children.size() != 2)
    return nullptr;

  for (int idx : {0, 1}) {
    int oIdx = (idx == 1) ? 0 : 1;
    auto oDiv = children[oIdx]->divided(2);
    if (oDiv == nullptr)
      continue;

    auto oDivNeg = oDiv->getCopy();
    oDivNeg->multiplyByMinusOne();

    auto node = children[idx];
    if (node->type == NodeType::NEGATION) {
      auto neg = node->children[0];
      if (neg->equals(*oDiv) || neg->equals(*oDivNeg))
        return neg;
    }

    if (oDiv->type == NodeType::NEGATION) {
      auto neg = oDiv->children[0];
      if (neg->equals(*node))
        return oDiv;
    }

    if (oDivNeg->type == NodeType::NEGATION) {
      auto neg = oDivNeg->children[0];
      if (neg->equals(*node))
        return oDivNeg;
    }

    auto neg = node->getOptTransformedNegated();
    if (neg != nullptr) {
      if (neg->equals(*oDiv) || neg->equals(*oDivNeg))
        return neg;
    }

    neg = oDiv->getOptTransformedNegated();
    if (neg != nullptr) {
      if (neg->equals(*node))
        return oDiv;
    }

    neg = oDivNeg->getOptTransformedNegated();
    if (neg != nullptr) {
      if (neg->equals(*node))
        return oDivNeg;
    }
  }

  return nullptr;
}

std::shared_ptr<Node> Node::getOptArgDisjConjIdentity2() {
  if (type != NodeType::NEGATION)
    return nullptr;

  auto child = children[0];
  if (child->type != NodeType::INCL_DISJUNCTION)
    return nullptr;
  if (child->children.size() != 2)
    return nullptr;

  for (int negIdx : {0, 1}) {
    auto ch = child->children[negIdx];

    std::shared_ptr<Node> node = nullptr;
    if (ch->type == NodeType::NEGATION)
      node = ch->children[0];
    else
      node = ch->getOptTransformedNegated();

    if (node == nullptr)
      continue;

    int oIdx = (negIdx == 1) ? 0 : 1;
    auto other = child->children[oIdx];

    if (node->isDouble(other))
      return other;

    auto neg = node->getCopy();
    neg->multiplyByMinusOne();
    if (neg->isDouble(other))
      return other;
  }

  return nullptr;
}

std::shared_ptr<Node> Node::divided(int64_t divisor) {
  if (type == NodeType::CONSTANT) {
    uint64_t low = MBAOps::toLow64(constant, bitCount);
    if (low % static_cast<uint64_t>(divisor) == 0)
      return newConstantNode(static_cast<int64_t>(low / static_cast<uint64_t>(divisor)));
  }

  if (type == NodeType::PRODUCT) {
    for (size_t i = 0; i < children.size(); ++i) {
      auto node = children[i]->divided(divisor);
      if (node == nullptr)
        continue;

      auto res = getCopy();
      res->children[i] = node;

      if (res->children[i]->isConstant(1)) {
        res->children.erase(res->children.begin() + i);
        if (res->children.size() == 1)
          return res->children[0];
      }

      return res;
    }
    return nullptr;
  }

  if (type == NodeType::SUM) {
    auto res = newNode(NodeType::SUM);
    for (auto &child : children) {
      auto node = child->divided(divisor);
      if (node == nullptr)
        return nullptr;
      res->children.push_back(node);
    }
    return res;
  }

  return nullptr;
}

bool Node::checkDisjConjIdentityRule2() {
  if (type != NodeType::INCL_DISJUNCTION)
    return false;

  bool changed = false;

  int i = -1;
  while (true) {
    ++i;
    if (i >= static_cast<int>(children.size()))
      break;

    auto child1 = children[i];
    auto node = child1->getOptArgDisjConjIdentityRule2();
    if (node == nullptr)
      continue;

    for (int j = 0; j < static_cast<int>(children.size()); ++j) {
      if (j == i)
        continue;

      if (children[j]->equals(*node)) {
        children.erase(children.begin() + i);
        changed = true;
        --i;
        break;
      }
    }
  }

  if (children.size() == 1)
    copy(*children[0]);
  return changed;
}

std::shared_ptr<Node> Node::getOptArgDisjConjIdentityRule2() {
  if (type != NodeType::CONJUNCTION)
    return nullptr;
  if (children.size() != 2)
    return nullptr;

  for (int idx : {0, 1}) {
    int oIdx = (idx == 1) ? 0 : 1;
    if (children[oIdx]->isDouble(children[idx])) {
      auto node = children[idx]->getCopy();
      node->multiplyByMinusOne();
      node->negate();
      return node;
    }
  }

  for (int idx : {0, 1}) {
    int oIdx = (idx == 1) ? 0 : 1;

    auto node = children[idx]->getCopy();
    node->multiplyByMinusOne();

    if (children[oIdx]->isDouble(node)) {
      node->negate();
      return node;
    }
  }

  return nullptr;
}

// --------------------------------------------------------- disj-neg-disj identity
bool Node::checkDisjNegDisjIdentityRule() {
  if (type != NodeType::INCL_DISJUNCTION)
    return false;

  bool changed = false;

  int i = -1;
  while (true) {
    ++i;
    if (i >= static_cast<int>(children.size()))
      break;

    auto child1 = children[i];
    auto node = child1->getOptArgDisjNegDisjIdentityRule();
    if (node == nullptr)
      continue;

    for (int j = 0; j < static_cast<int>(children.size()); ++j) {
      if (j == i)
        continue;

      if (children[j]->equals(*node)) {
        children.erase(children.begin() + i);
        changed = true;
        --i;
        break;
      }
    }
  }

  if (children.size() == 1)
    copy(*children[0]);
  return changed;
}

std::shared_ptr<Node> Node::getOptArgDisjNegDisjIdentityRule() {
  if (type != NodeType::PRODUCT)
    return nullptr;
  if (children.size() != 2)
    return nullptr;
  if (!children[0]->isConstant(-1))
    return nullptr;

  auto disj = children[1];
  if (disj->type != NodeType::INCL_DISJUNCTION)
    return nullptr;
  if (disj->children.size() != 2)
    return nullptr;

  for (int idx : {0, 1}) {
    int oIdx = (idx == 1) ? 0 : 1;
    if (disj->children[oIdx]->isDouble(disj->children[idx])) {
      auto node = disj->children[idx]->getCopy();
      node->multiplyByMinusOne();
      return node;
    }
  }

  for (int idx : {0, 1}) {
    int oIdx = (idx == 1) ? 0 : 1;

    auto node = disj->children[idx]->getCopy();
    node->multiplyByMinusOne();

    if (disj->children[oIdx]->isDouble(node))
      return node;
  }

  return nullptr;
}

// --------------------------------------------------------- conj-disj identity
bool Node::checkConjDisjIdentityRule() {
  if (type != NodeType::CONJUNCTION)
    return false;

  bool changed = false;

  int i = -1;
  while (true) {
    ++i;
    if (i >= static_cast<int>(children.size()))
      break;

    auto child1 = children[i];
    auto node = child1->getOptArgConjDisjIdentity();
    if (node == nullptr)
      continue;

    for (int j = 0; j < static_cast<int>(children.size()); ++j) {
      if (j == i)
        continue;

      auto child2 = children[j];
      if (child2->equals(*node)) {
        children.erase(children.begin() + i);
        changed = true;
        --i;
        break;
      }
    }
  }

  if (children.size() == 1)
    copy(*children[0]);
  return changed;
}

std::shared_ptr<Node> Node::getOptArgConjDisjIdentity() {
  auto node = getOptArgConjDisjIdentity1();
  return node != nullptr ? node : getOptArgConjDisjIdentity2();
}

std::shared_ptr<Node> Node::getOptArgConjDisjIdentity1() {
  if (children.size() != 2)
    return nullptr;

  auto child0 = children[0]->getCopy();
  auto child1 = children[1]->getCopy();

  child0->multiplyByMinusOne();
  child1->multiplyByMinusOne();

  child0->negate();
  child1->negate();

  if (child0->isDouble(child1))
    return child1;
  if (child1->isDouble(child0))
    return child0;

  return nullptr;
}

std::shared_ptr<Node> Node::getOptArgConjDisjIdentity2() {
  if (children.size() != 2)
    return nullptr;

  for (int idx : {0, 1}) {
    auto neg = children[idx]->getCopy();
    neg->negate();

    int oIdx = (idx == 1) ? 0 : 1;
    auto other = children[oIdx];

    bool ok = neg->isDouble(other);
    if (!ok) {
      neg->multiplyByMinusOne();
      ok = neg->isDouble(other);
    }

    if (!ok)
      continue;

    auto node = other->getCopy();
    node->multiplyByMinusOne();
    node->negate();
    return node;
  }

  return nullptr;
}

// --------------------------------------------------------- sub identities
bool Node::checkDisjSubDisjIdentityRule() {
  if (type != NodeType::INCL_DISJUNCTION)
    return false;

  bool changed = false;

  int i = -1;
  while (true) {
    ++i;
    if (i >= static_cast<int>(children.size()))
      break;

    auto child1 = children[i];
    auto node = child1->getOptArgDisjSubDisjIdentity();
    if (node == nullptr)
      continue;

    for (int j = 0; j < static_cast<int>(children.size()); ++j) {
      if (j == i)
        continue;

      if (children[j]->equals(*node)) {
        children.erase(children.begin() + i);
        changed = true;
        --i;
        break;
      }
    }
  }

  if (children.size() == 1)
    copy(*children[0]);
  return changed;
}

std::shared_ptr<Node> Node::getOptArgDisjSubDisjIdentity() {
  if (type != NodeType::SUM)
    return nullptr;
  if (children.size() != 2)
    return nullptr;

  for (int idx : {0, 1}) {
    auto child = children[idx];
    if (child->type != NodeType::INCL_DISJUNCTION)
      continue;
    if (child->children.size() != 2)
      continue;

    int oidx = (idx == 1) ? 0 : 1;
    auto neg = children[oidx]->getCopy();
    neg->multiplyByMinusOne();

    if (neg->equals(*child->children[0]))
      return child->children[1];
    if (neg->equals(*child->children[1]))
      return child->children[0];
  }

  return nullptr;
}

bool Node::checkDisjSubConjIdentityRule() {
  if (type != NodeType::INCL_DISJUNCTION)
    return false;

  bool changed = false;

  int i = -1;
  while (true) {
    ++i;
    if (i >= static_cast<int>(children.size()))
      break;

    auto child1 = children[i];
    auto node = child1->getOptArgDisjSubConjIdentity();
    if (node == nullptr)
      continue;

    for (int j = 0; j < static_cast<int>(children.size()); ++j) {
      if (j == i)
        continue;

      if (children[j]->equals(*node)) {
        children.erase(children.begin() + i);
        changed = true;
        --i;
        break;
      }
    }
  }

  if (children.size() == 1)
    copy(*children[0]);
  return changed;
}

std::shared_ptr<Node> Node::getOptArgDisjSubConjIdentity() {
  if (type != NodeType::SUM)
    return nullptr;
  if (children.size() != 2)
    return nullptr;

  for (int idx : {0, 1}) {
    auto child = children[idx];
    if (child->type != NodeType::PRODUCT)
      continue;
    if (child->children.size() != 2)
      continue;
    if (!child->children[0]->isConstant(-1))
      continue;

    auto conj = child->children[1];
    if (conj->type != NodeType::CONJUNCTION)
      continue;

    int oidx = (idx == 1) ? 0 : 1;
    auto other = children[oidx];

    for (auto &c : conj->children)
      if (c->equals(*other))
        return other;
  }

  return nullptr;
}

bool Node::checkConjAddConjIdentityRule() {
  if (type != NodeType::CONJUNCTION)
    return false;

  bool changed = false;

  int i = -1;
  while (true) {
    ++i;
    if (i >= static_cast<int>(children.size()))
      break;

    auto child1 = children[i];
    auto node = child1->getOptArgConjAddConjIdentity();
    if (node == nullptr)
      continue;

    for (int j = 0; j < static_cast<int>(children.size()); ++j) {
      if (j == i)
        continue;

      if (children[j]->equals(*node)) {
        children.erase(children.begin() + i);
        changed = true;
        --i;
        break;
      }
    }
  }

  if (children.size() == 1)
    copy(*children[0]);
  return changed;
}

std::shared_ptr<Node> Node::getOptArgConjAddConjIdentity() {
  if (type != NodeType::SUM)
    return nullptr;
  if (children.size() != 2)
    return nullptr;

  for (int idx : {0, 1}) {
    auto child = children[idx];
    if (child->type != NodeType::CONJUNCTION)
      continue;

    int oidx = (idx == 1) ? 0 : 1;
    auto oneg = children[oidx]->getCopy();
    oneg->negate();

    for (auto &c : child->children)
      if (c->equals(*oneg))
        return children[oidx];
  }

  return nullptr;
}

// --------------------------------------------------------- nested bitwise rule
bool Node::checkConjConjDisjRule() { return checkNestedBitwiseRule(NodeType::CONJUNCTION); }

bool Node::checkDisjDisjConjRule() { return checkNestedBitwiseRule(NodeType::INCL_DISJUNCTION); }

bool Node::checkNestedBitwiseRule(NodeType t) {
  if (type != t)
    return false;
  bool changed = false;

  int i = -1;
  while (true) {
    ++i;
    if (i >= static_cast<int>(children.size()))
      break;

    auto child1 = children[i];
    auto pr = child1->getOptArgNestedBitwise(t);
    auto node1 = pr.first;
    auto node2 = pr.second;
    if (node1 == nullptr)
      continue;

    for (int j = 0; j < static_cast<int>(children.size()); ++j) {
      if (j == i)
        continue;

      if (children[j]->equals(*node1)) {
        children[i]->copy(*node2);
        changed = true;
        break;
      }
    }
  }

  if (children.size() == 1)
    copy(*children[0]);
  return changed;
}

std::pair<std::shared_ptr<Node>, std::shared_ptr<Node>> Node::getOptArgNestedBitwise(NodeType t) {
  if (type != NodeType::PRODUCT)
    return {nullptr, nullptr};
  if (children.size() != 2)
    return {nullptr, nullptr};
  if (!children[0]->isConstant(-1))
    return {nullptr, nullptr};

  auto child = children[1];
  if (child->type != t)
    return {nullptr, nullptr};
  if (child->children.size() != 2)
    return {nullptr, nullptr};

  for (int idx : {0, 1}) {
    int oidx = (idx == 1) ? 0 : 1;

    auto c = child->children[idx];
    NodeType ot = (t == NodeType::INCL_DISJUNCTION) ? NodeType::CONJUNCTION : NodeType::INCL_DISJUNCTION;
    if (c->type != ot)
      continue;
    if (c->children.size() != 2)
      return {nullptr, nullptr};

    auto oneg = child->children[oidx]->getCopy();
    oneg->multiplyByMinusOne();

    if (c->children[0]->equals(*oneg))
      return {c->children[1], c->children[0]};
    if (c->children[1]->equals(*oneg))
      return {c->children[0], c->children[1]};
  }

  return {nullptr, nullptr};
}

bool Node::checkDisjDisjConjRule2() {
  if (type != NodeType::INCL_DISJUNCTION)
    return false;

  bool changed = false;

  int i = -1;
  while (true) {
    ++i;
    if (i >= static_cast<int>(children.size()))
      break;

    auto child1 = children[i];
    auto pr = child1->getOptPairDisjDisjConj2();
    auto node = pr.first;
    auto conj = pr.second;
    if (node == nullptr)
      continue;

    for (int j = 0; j < static_cast<int>(children.size()); ++j) {
      if (j == i)
        continue;

      auto child2 = children[j];

      if (child2->equals(*node)) {
        children.erase(children.begin() + i);
        changed = true;
        --i;
        break;
      }

      if (child2->type != NodeType::CONJUNCTION)
        continue;
      if (!child2->hasChild(*node))
        continue;

      if (areAllChildrenContained(child2->children, conj->children)) {
        children[j] = node->getCopy();
        children.erase(children.begin() + i);
        changed = true;
        --i;
        break;
      }
    }
  }

  if (children.size() == 1)
    copy(*children[0]);
  return changed;
}

std::pair<std::shared_ptr<Node>, std::shared_ptr<Node>> Node::getOptPairDisjDisjConj2() {
  if (type != NodeType::PRODUCT)
    return {nullptr, nullptr};
  if (children.size() != 2)
    return {nullptr, nullptr};
  if (!children[0]->isConstant(-1))
    return {nullptr, nullptr};

  auto disj = children[1];
  if (disj->type != NodeType::INCL_DISJUNCTION)
    return {nullptr, nullptr};
  if (disj->children.size() != 2)
    return {nullptr, nullptr};

  for (int idx : {0, 1}) {
    int oIdx = (idx == 1) ? 0 : 1;

    auto conj = disj->children[idx];
    if (conj->type != NodeType::CONJUNCTION)
      continue;

    auto neg = disj->children[oIdx]->getCopy();
    neg->multiplyByMinusOne();

    if (conj->hasChild(*neg))
      return {neg, conj};
  }

  return {nullptr, nullptr};
}

} // namespace MBA
} // namespace LSiMBA
