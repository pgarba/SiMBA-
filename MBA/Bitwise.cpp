// GAMBA native C++ port — Bitwise AST implementation.
#include "Bitwise.h"

#include <algorithm>

namespace LSiMBA {
namespace MBA {

Bitwise::Bitwise(BitwiseType bType, bool negated, int vidx)
    : type(bType), vidx(vidx), negated(negated) {}

void Bitwise::addChild(std::shared_ptr<Bitwise> child) { children.push_back(child); }

void Bitwise::addVariable(int vidx, bool negated) {
  children.push_back(std::make_shared<Bitwise>(BitwiseType::VARIABLE, negated, vidx));
}

std::string Bitwise::opToString() const {
  if (type == BitwiseType::CONJUNCTION)
    return "&";
  if (type == BitwiseType::EXCL_DISJUNCTION)
    return "^";
  return "|";
}

std::string Bitwise::toString(const std::vector<std::string> &variables,
                              bool withParentheses) const {
  if (type == BitwiseType::TRUE)
    return negated ? "0" : "1";

  if (type == BitwiseType::VARIABLE) {
    if (variables.empty())
      return std::string(negated ? "~x" : "x") + std::to_string(vidx);
    return std::string(negated ? "~" : "") + variables[vidx];
  }

  withParentheses = withParentheses || negated;

  std::string s;
  if (negated)
    s += "~";
  if (withParentheses)
    s += "(";
  s += children[0]->toString(variables, true);
  for (size_t i = 1; i < children.size(); ++i)
    s += opToString() + children[i]->toString(variables, true);
  if (withParentheses)
    s += ")";

  return s;
}

bool Bitwise::areAllChildrenContained(const Bitwise &other) const {
  std::vector<int> oIndices;
  for (size_t i = 0; i < other.children.size(); ++i)
    oIndices.push_back(static_cast<int>(i));

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

bool Bitwise::equals(const Bitwise &other, bool negated) const {
  if (type != other.type)
    return false;
  if (vidx != other.vidx)
    return false;
  if ((this->negated == other.negated) == negated)
    return false;
  if (children.size() != other.children.size())
    return false;

  return areAllChildrenContained(other);
}

void Bitwise::pullUpChild() {
  auto child = children[0];
  type = child->type;
  vidx = child->vidx;
  negated = negated != child->negated;
  children = child->children;
}

void Bitwise::copy(const Bitwise &node) {
  type = node.type;
  vidx = node.vidx;
  negated = node.negated;
  children = node.children;
}

std::shared_ptr<Bitwise> Bitwise::getCopy() const {
  auto n = std::make_shared<Bitwise>(type, negated, vidx);
  for (auto &child : children)
    n->children.push_back(child->getCopy());
  return n;
}

void Bitwise::refine() {
  constexpr int MAX_IT = 10;
  for (int i = 0; i < MAX_IT; ++i)
    if (!refineStep())
      return;
}

bool Bitwise::refineStep() {
  bool changed = false;

  for (auto &child : children)
    if (child->refineStep())
      changed = true;

  if (checkInsertXor())
    changed = true;
  if (checkFlipNegation())
    changed = true;
  if (checkExtract())
    changed = true;

  return changed;
}

bool Bitwise::checkInsertXor() {
  if (type != BitwiseType::CONJUNCTION && type != BitwiseType::INCL_DISJUNCTION)
    return false;

  bool changed = false;
  for (int i = 0; i < static_cast<int>(children.size()) - 1; ++i) {
    if (i >= static_cast<int>(children.size()) - 1)
      break;

    for (int j = 1; j < static_cast<int>(children.size()); ++j) {
      if (tryInsertXor(i, j)) {
        changed = true;
        break;
      }
    }
  }

  if (changed && children.size() == 1)
    pullUpChild();

  return changed;
}

bool Bitwise::tryInsertXor(int i, int j) {
  auto child1 = children[i];
  auto child2 = children[j];

  auto t = type;
  auto ot = (t == BitwiseType::CONJUNCTION) ? BitwiseType::INCL_DISJUNCTION
                                            : BitwiseType::CONJUNCTION;

  if (child1->type != ot || child2->type != ot)
    return false;

  if (child1->children.size() != 2 || child2->children.size() != 2)
    return false;

  int perms[2][2] = {{0, 1}, {1, 0}};
  for (int p = 0; p < 2; ++p) {
    if (child1->children[0]->equals(*child2->children[perms[p][0]], true)) {
      if (!child1->children[1]->equals(*child2->children[perms[p][1]], true))
        return false;

      child1->type = BitwiseType::EXCL_DISJUNCTION;

      if (t == BitwiseType::CONJUNCTION) {
        if (child1->children[0]->negated) {
          child1->children[0]->negated = false;
          child1->children[1]->negated = !child1->children[1]->negated;
        }
      } else {
        if (child1->children[0]->negated)
          child1->children[0]->negated = false;
        else
          child1->children[1]->negated = !child1->children[1]->negated;
      }

      children.erase(children.begin() + j);
      return true;
    }
  }

  return false;
}

bool Bitwise::checkFlipNegation() {
  if (children.empty())
    return false;

  bool changed = false;

  for (auto &child : children)
    if (child->checkFlipNegation())
      changed = true;

  int cnt = static_cast<int>(children.size());
  int negCnt = 0;
  for (auto &c : children)
    if (c->negated)
      negCnt++;

  if (2 * negCnt < cnt)
    return changed;
  if (2 * negCnt == cnt && (!negated || type == BitwiseType::EXCL_DISJUNCTION))
    return changed;

  if (type != BitwiseType::EXCL_DISJUNCTION) {
    negated = !negated;
    if (type == BitwiseType::INCL_DISJUNCTION)
      type = BitwiseType::CONJUNCTION;
    else
      type = BitwiseType::INCL_DISJUNCTION;
  }

  for (auto &child : children)
    child->negated = !child->negated;

  return true;
}

bool Bitwise::doAllChildrenHaveType(BitwiseType t) const {
  for (auto &child : children) {
    if (child->type != t)
      return false;
    if (child->negated)
      return false;
  }
  return true;
}

bool Bitwise::checkExtract() {
  auto t = type;
  if (t != BitwiseType::CONJUNCTION && t != BitwiseType::INCL_DISJUNCTION)
    return false;

  auto ot = (t == BitwiseType::CONJUNCTION) ? BitwiseType::INCL_DISJUNCTION
                                            : BitwiseType::CONJUNCTION;
  if (!doAllChildrenHaveType(ot))
    return false;

  std::vector<std::shared_ptr<Bitwise>> commons;
  while (true) {
    auto common = tryExtract();
    if (common == nullptr)
      break;
    commons.push_back(common);
  }

  if (commons.empty())
    return false;

  for (size_t i = 0; i < children.size(); ++i) {
    if (children[i]->children.size() == 1)
      children[i]->pullUpChild();
  }

  auto node = std::make_shared<Bitwise>(ot, negated);
  negated = false;
  node->children = commons;
  node->children.push_back(getCopy());
  copy(*node);

  return true;
}

std::shared_ptr<Bitwise> Bitwise::tryExtract() {
  auto common = getCommonChild();
  if (common == nullptr)
    return nullptr;

  for (auto &child : children)
    child->removeChild(*common);

  return common;
}

std::shared_ptr<Bitwise> Bitwise::getCommonChild() {
  auto first = children[0];
  for (auto &child : first->children) {
    if (hasChildInRemainingChildren(*child))
      return child->getCopy();
  }
  return nullptr;
}

bool Bitwise::hasChildInRemainingChildren(const Bitwise &node) {
  for (size_t i = 1; i < children.size(); ++i)
    if (!children[i]->hasChild(node))
      return false;
  return true;
}

bool Bitwise::hasChild(const Bitwise &node) const {
  for (auto &child : children)
    if (child->equals(node))
      return true;
  return false;
}

void Bitwise::removeChild(const Bitwise &node) {
  for (size_t i = 0; i < children.size(); ++i) {
    if (children[i]->equals(node)) {
      children.erase(children.begin() + i);
      return;
    }
  }
}

} // namespace MBA
} // namespace LSiMBA
