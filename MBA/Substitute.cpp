// GAMBA native C++ port — substitution methods.
// Mirrors external/GAMBA/src/utils/node.py.
#include "Node.h"

namespace LSiMBA {
namespace MBA {

int Node::getIndexInList(const std::vector<std::shared_ptr<Node>> &l) const {
  for (size_t i = 0; i < l.size(); ++i)
    if (equals(*l[i]))
      return static_cast<int>(i);
  return -1;
}

bool Node::isContained(const std::vector<std::shared_ptr<Node>> &l) const {
  return getIndexInList(l) != -1;
}

std::shared_ptr<Node> Node::getNodeForSubstitution(
    const std::vector<std::shared_ptr<Node>> &ignoreList) {
  if (isContained(ignoreList))
    return nullptr;

  if (isBitwiseOp()) {
    for (auto &child : children) {
      if (child->type == NodeType::CONSTANT || child->isArithmOp()) {
        if (!child->isContained(ignoreList))
          return child->getCopy();
      } else if (child->isBitwiseOp()) {
        auto node = child->getNodeForSubstitution(ignoreList);
        if (node != nullptr)
          return node;
      }
    }
    return nullptr;
  }

  if (type == NodeType::POWER)
    return children[0]->getNodeForSubstitution(ignoreList);

  for (auto &child : children) {
    auto node = child->getNodeForSubstitution(ignoreList);
    if (node != nullptr)
      return node;
  }

  return nullptr;
}

bool Node::substituteAllOccurences(const std::shared_ptr<Node> &node,
                                   const std::string &vname, bool onlyFullMatch,
                                   bool withMod) {
  if (type == NodeType::POWER)
    return children[0]->substituteAllOccurences(node, vname, onlyFullMatch, withMod);

  bool changed = false;
  bool bitwise = isBitwiseOp();

  std::shared_ptr<Node> inv = nullptr;
  if (!bitwise && !onlyFullMatch && withMod) {
    inv = node->getCopy();
    inv->multiplyByMinusOne();
  }

  for (auto &child : children) {
    auto r = child->trySubstituteNode(node, vname, bitwise);
    if (r.first)
      changed = true;
    if (r.second)
      continue;

    if (!bitwise && !onlyFullMatch && withMod) {
      auto r2 = child->trySubstituteNode(inv, vname, false, true);
      if (r2.first)
        changed = true;
      if (r2.second)
        continue;

      if (child->trySubstitutePartOfSum(node, vname)) {
        changed = true;
        continue;
      }

      if (child->trySubstitutePartOfSum(inv, vname, true)) {
        changed = true;
        continue;
      }
    }

    if (bitwise && !child->isBitwiseOp())
      continue;

    if (child->substituteAllOccurences(node, vname, onlyFullMatch, withMod))
      changed = true;
  }

  return changed;
}

std::pair<bool, bool> Node::trySubstituteNode(const std::shared_ptr<Node> &node,
                                              const std::string &vname, bool onlyFull,
                                              bool inverse) {
  if (equals(*node)) {
    auto var = newVariableNode(vname);
    if (inverse)
      var->multiplyByMinusOne();
    copy(*var);
    return {true, true};
  }

  if (onlyFull)
    return {false, false};

  if (node->children.size() > 1 && node->type == type &&
      children.size() > node->children.size()) {
    if (areAllChildrenContained(node->children, children)) {
      removeChildrenOfNode(*node);
      auto var = newVariableNode(vname);
      if (inverse)
        var->multiplyByMinusOne();
      children.push_back(var);
      return {true, false};
    }
  }

  return {false, false};
}

bool Node::trySubstitutePartOfSum(const std::shared_ptr<Node> &node,
                                  const std::string &vname, bool inverse) {
  if (node->type != NodeType::SUM || node->children.size() <= 1)
    return false;

  if (type == NodeType::SUM)
    return trySubstitutePartOfSumInSum(node, vname, inverse);
  return trySubstitutePartOfSumTerm(node, vname, inverse);
}

bool Node::trySubstitutePartOfSumInSum(const std::shared_ptr<Node> &node,
                                       const std::string &vname, bool inverse) {
  auto common = getCommonChildren(*node);
  if (common.empty())
    return false;

  for (auto &c : common)
    children.erase(std::find(children.begin(), children.end(), c));
  children.push_back(newVariableNode(vname));
  if (inverse)
    children.back()->multiplyByMinusOne();

  for (auto &c : node->children) {
    bool found = false;
    for (auto it = common.begin(); it != common.end(); ++it) {
      if (c->equals(**it)) {
        common.erase(it);
        found = true;
        break;
      }
    }

    if (!found) {
      auto n = c->getCopy();
      n->multiplyByMinusOne();
      children.push_back(n);
    }
  }

  return true;
}

std::vector<std::shared_ptr<Node>> Node::getCommonChildren(const Node &other) const {
  std::vector<std::shared_ptr<Node>> common;
  std::vector<int> oIndices;
  for (size_t i = 0; i < other.children.size(); ++i)
    oIndices.push_back(static_cast<int>(i));

  for (auto &child : children) {
    for (auto it = oIndices.begin(); it != oIndices.end(); ++it) {
      if (child->equals(*other.children[*it])) {
        oIndices.erase(it);
        common.push_back(child);
        break;
      }
    }
  }

  return common;
}

bool Node::trySubstitutePartOfSumTerm(const std::shared_ptr<Node> &node,
                                      const std::string &vname, bool inverse) {
  if (!node->hasChild(*this))
    return false;

  auto var = newVariableNode(vname);
  if (inverse)
    var->multiplyByMinusOne();
  auto sumNode = newNodeWithChildren(NodeType::SUM, {var});

  bool found = false;
  for (auto &c : node->children) {
    if (!found && equals(*c)) {
      found = true;
      continue;
    }

    auto n = c->getCopy();
    n->multiplyByMinusOne();
    sumNode->children.push_back(n);
  }

  copy(*sumNode);
  return true;
}

void Node::removeChildrenOfNode(const Node &other) {
  for (auto &ochild : other.children) {
    for (size_t i = 0; i < children.size(); ++i) {
      if (children[i]->equals(*ochild)) {
        children.erase(children.begin() + i);
        break;
      }
    }
  }
}

} // namespace MBA
} // namespace LSiMBA
