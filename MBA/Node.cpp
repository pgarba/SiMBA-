// GAMBA native C++ port — Node implementation.
#include "Node.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace LSiMBA {
namespace MBA {

using namespace std;

// Mirrors node.py Node.to_string.
string Node::toString(bool withParentheses, int end,
                      const vector<string> *varNames) {
  if (end == -1)
    end = static_cast<int>(children.size());

  if (type == NodeType::CONSTANT)
    return MBAOps::toStringSigned(constant);

  if (type == NodeType::VARIABLE)
    return varNames == nullptr ? vname : (*varNames)[vidx];

  if (type == NodeType::POWER) {
    auto child1 = children[0];
    auto child2 = children[1];
    string ret = child1->toString(typeRank(child1->type) > typeRank(NodeType::VARIABLE), -1,
                                  varNames) +
                 "**" +
                 child2->toString(typeRank(child2->type) > typeRank(NodeType::VARIABLE), -1,
                                  varNames);
    if (withParentheses)
      ret = "(" + ret + ")";
    return ret;
  }

  if (type == NodeType::NEGATION) {
    auto child = children[0];
    string ret = "~" +
                child->toString(typeRank(child->type) > typeRank(NodeType::NEGATION), -1,
                                varNames);
    if (withParentheses)
      ret = "(" + ret + ")";
    return ret;
  }

  if (type == NodeType::PRODUCT) {
    auto child1 = children[0];
    string ret1 =
        child1->toString(typeRank(child1->type) > typeRank(NodeType::PRODUCT), -1, varNames);
    string ret = ret1;
    for (int i = 1; i < end; ++i) {
      ret += "*" +
             children[i]->toString(
                 typeRank(children[i]->type) > typeRank(NodeType::PRODUCT), -1, varNames);
    }
    // Rather than multiplying by -1, only use the minus and get rid of '1*'.
    if (ret1 == "-1" && children.size() > 1 && end > 1)
      ret = "-" + ret.substr(3);
    if (withParentheses)
      ret = "(" + ret + ")";
    return ret;
  }

  if (type == NodeType::SUM) {
    auto child1 = children[0];
    string ret = child1->toString(typeRank(child1->type) > typeRank(NodeType::SUM), -1, varNames);
    for (int i = 1; i < end; ++i) {
      string s =
          children[i]->toString(typeRank(children[i]->type) > typeRank(NodeType::SUM), -1,
                                varNames);
      if (!s.empty() && s[0] != '-')
        ret += "+";
      ret += s;
    }
    if (withParentheses)
      ret = "(" + ret + ")";
    return ret;
  }

  if (type == NodeType::CONJUNCTION) {
    auto child1 = children[0];
    string ret =
        child1->toString(typeRank(child1->type) > typeRank(NodeType::CONJUNCTION), -1, varNames);
    for (int i = 1; i < end; ++i) {
      ret += "&" +
             children[i]->toString(
                 typeRank(children[i]->type) > typeRank(NodeType::CONJUNCTION), -1, varNames);
    }
    if (withParentheses)
      ret = "(" + ret + ")";
    return ret;
  }

  if (type == NodeType::EXCL_DISJUNCTION) {
    auto child1 = children[0];
    string ret = child1->toString(
        typeRank(child1->type) > typeRank(NodeType::EXCL_DISJUNCTION), -1, varNames);
    for (int i = 1; i < end; ++i) {
      ret += "^" +
             children[i]->toString(
                 typeRank(children[i]->type) > typeRank(NodeType::EXCL_DISJUNCTION), -1, varNames);
    }
    if (withParentheses)
      ret = "(" + ret + ")";
    return ret;
  }

  if (type == NodeType::INCL_DISJUNCTION) {
    auto child1 = children[0];
    string ret = child1->toString(
        typeRank(child1->type) > typeRank(NodeType::INCL_DISJUNCTION), -1, varNames);
    for (int i = 1; i < end; ++i) {
      ret += "|" +
             children[i]->toString(
                 typeRank(children[i]->type) > typeRank(NodeType::INCL_DISJUNCTION), -1, varNames);
    }
    if (withParentheses)
      ret = "(" + ret + ")";
    return ret;
  }

  return "<invalid>";
}

// Mirrors node.py collect_and_enumerate_variables / collect_variables /
// enumerate_variables.
void Node::collectAndEnumerateVariables(vector<string> &variables) {
  collectVariables(variables);
  std::sort(variables.begin(), variables.end(), [](const string &a, const string &b) {
    if (a.size() != b.size())
      return a.size() < b.size();
    return a < b;
  });
  enumerateVariables(variables);
}

void Node::collectVariables(vector<string> &variables) {
  if (type == NodeType::VARIABLE) {
    if (find(variables.begin(), variables.end(), vname) == variables.end())
      variables.push_back(vname);
  } else {
    for (auto &child : children)
      child->collectVariables(variables);
  }
}

void Node::enumerateVariables(const vector<string> &variables) {
  if (type == NodeType::VARIABLE) {
    auto it = find(variables.begin(), variables.end(), vname);
    vidx = (it == variables.end()) ? -1 : static_cast<int>(it - variables.begin());
  } else {
    for (auto &child : children)
      child->enumerateVariables(variables);
  }
}

int Node::getMaxVname(const string &start, const string &end) {
  if (type == NodeType::VARIABLE) {
    if (vname.substr(0, start.size()) != start)
      return -1;
    if (vname.size() < end.size() ||
        vname.substr(vname.size() - end.size()) != end)
      return -1;
    string n = vname.substr(start.size(), vname.size() - start.size() - end.size());
    if (n.empty())
      return -1;
    for (char c : n)
      if (!isdigit(static_cast<unsigned char>(c)))
        return -1;
    return atoi(n.c_str());
  }
  int maxn = -1;
  for (auto &child : children) {
    int n = child->getMaxVname(start, end);
    if (n != -1 && (maxn == -1 || n > maxn))
      maxn = n;
  }
  return maxn;
}

// Mirrors node.py eval / __apply_binop / __apply_bitwise_binop.
uint64_t Node::eval(const vector<uint64_t> &X) {
  if (type == NodeType::CONSTANT)
    return MBAOps::toLow64(constant, bitCount);

  if (type == NodeType::VARIABLE) {
    if (vidx < 0)
      return 0; // Python would sys.exit; treat as 0 for safety.
    return MBAOps::reduce(X[vidx], bitCount);
  }

  if (type == NodeType::NEGATION)
    return MBAOps::reduce(static_cast<uint64_t>(~children[0]->eval(X)), bitCount);

  uint64_t val = children[0]->eval(X);
  for (size_t i = 1; i < children.size(); ++i)
    val = MBAOps::reduce(applyBinop(val, children[i]->eval(X)), bitCount);

  return val;
}

uint64_t Node::applyBinop(uint64_t x, uint64_t y) {
  if (type == NodeType::POWER)
    return power(x, y);
  if (type == NodeType::PRODUCT)
    return x * y;
  if (type == NodeType::SUM)
    return x + y;
  if (type == NodeType::CONJUNCTION)
    return x & y;
  if (type == NodeType::EXCL_DISJUNCTION)
    return x ^ y;
  if (type == NodeType::INCL_DISJUNCTION)
    return x | y;
  return 0;
}

// ===================================================== Phase 2: queries
bool Node::hasNonlinearChild() {
  for (auto &child : children)
    if (child->state == NodeState::NONLINEAR || child->state == NodeState::MIXED)
      return true;
  return false;
}

bool Node::isLinear() const {
  return state == NodeState::BITWISE || state == NodeState::LINEAR;
}

bool Node::isBitwiseOp() const { return type == NodeType::NEGATION || isBitwiseBinop(); }

bool Node::isBitwiseBinop() const {
  return type == NodeType::CONJUNCTION || type == NodeType::EXCL_DISJUNCTION ||
         type == NodeType::INCL_DISJUNCTION;
}

bool Node::isArithmOp() const {
  return type == NodeType::SUM || type == NodeType::PRODUCT || type == NodeType::POWER;
}

// Mirrors node.py Node.__lt__.
bool Node::lessThan(const Node &other) const {
  if (type == NodeType::CONSTANT)
    return true;
  if (other.type == NodeType::CONSTANT)
    return false;

  std::string vn1 = getExtendedVariable();
  std::string vn2 = other.getExtendedVariable();
  bool has1 = !vn1.empty();
  bool has2 = !vn2.empty();
  if (has1) {
    if (!has2)
      return true;
    if (vn1 != vn2)
      return vn1 < vn2;
    return type == NodeType::VARIABLE;
  }

  if (has2)
    return false;

  if (type != other.type)
    return type < other.type;
  return children.size() < other.children.size();
}

// ===================================================== Phase 2: equality
bool Node::equals(const Node &other) const {
  if (type != other.type)
    return equalsRewritingBitwise(other);
  if (type == NodeType::CONSTANT)
    return constant == other.constant;
  if (type == NodeType::VARIABLE)
    return vname == other.vname;
  if (children.size() != other.children.size())
    return false;
  return areAllChildrenContained(children, other.children);
}

bool Node::equalsNegated(const Node &other) const {
  if (type == NodeType::NEGATION && children[0]->equals(other))
    return true;
  if (other.type == NodeType::NEGATION && other.children[0]->equals(*this))
    return true;
  return false;
}

bool Node::equalsRewritingBitwise(const Node &other) const {
  return equalsRewritingBitwiseAsymm(other) ||
         other.equalsRewritingBitwiseAsymm(*this);
}

bool Node::equalsRewritingBitwiseAsymm(const Node &other) const {
  if (type == NodeType::NEGATION) {
    auto node = other.getOptTransformedNegated();
    return node != nullptr && node->equals(*children[0]);
  }
  if (type == NodeType::PRODUCT) {
    if (children.size() != 2)
      return false;
    if (!children[0]->isConstant(-1))
      return false;
    if (children[1]->type != NodeType::NEGATION)
      return false;
    auto node = other.getOptNegativeTransformedNegated();
    return node != nullptr && node->equals(*children[1]->children[0]);
  }
  return false;
}

std::shared_ptr<Node> Node::getOptTransformedNegated() const {
  if (type == NodeType::SUM)
    return getOptTransformedNegatedSum();
  if (type == NodeType::PRODUCT)
    return getOptTransformedNegatedProduct();
  return nullptr;
}

std::shared_ptr<Node> Node::getOptTransformedNegatedSum() const {
  if (type != NodeType::SUM)
    return nullptr;
  if (children.size() < 2)
    return nullptr;
  if (!children[0]->isConstant(-1))
    return nullptr;
  auto res = newNode(NodeType::SUM);
  for (size_t i = 1; i < children.size(); ++i) {
    auto child = children[i];
    bool hasMinusOne =
        child->type == NodeType::PRODUCT && child->children[0]->isConstant(-1);
    if (hasMinusOne) {
      if (child->children.size() == 2) {
        res->children.push_back(child->children[1]);
        continue;
      }
      std::vector<std::shared_ptr<Node>> rest(child->children.begin() + 1,
                                              child->children.end());
      res->children.push_back(newNodeWithChildren(NodeType::PRODUCT, rest));
    } else {
      auto node = child->getCopy();
      node->multiplyByMinusOne();
      res->children.push_back(node);
    }
  }
  if (res->children.size() == 1)
    return res->children[0];
  return res;
}

std::shared_ptr<Node> Node::getOptTransformedNegatedProduct() const {
  if (type != NodeType::PRODUCT)
    return nullptr;
  if (children.size() != 2)
    return nullptr;
  if (!children[0]->isConstant(-1))
    return nullptr;
  auto child1 = children[1];
  if (child1->type != NodeType::SUM)
    return nullptr;
  if (!child1->children[0]->isConstant(1))
    return nullptr;
  if (child1->children.size() < 2)
    return nullptr;
  if (child1->children.size() == 2)
    return child1->children[1];
  std::vector<std::shared_ptr<Node>> rest(child1->children.begin() + 1,
                                          child1->children.end());
  return newNodeWithChildren(NodeType::SUM, rest);
}

std::shared_ptr<Node> Node::getOptNegativeTransformedNegated() const {
  if (type != NodeType::SUM)
    return nullptr;
  if (children.size() < 2)
    return nullptr;
  if (!children[0]->isConstant(1))
    return nullptr;
  auto res = newNode(NodeType::SUM);
  for (size_t i = 1; i < children.size(); ++i)
    res->children.push_back(children[i]->getCopy());
  if (res->children.size() == 1)
    return res->children[0];
  return res;
}

// ===================================================== Phase 2: mark linear
void Node::markLinear(bool restrictedScope) {
  for (auto &c : children)
    if (!restrictedScope || c->state == NodeState::UNKNOWN)
      c->markLinear();

  if (type == NodeType::INCL_DISJUNCTION || type == NodeType::EXCL_DISJUNCTION ||
      type == NodeType::CONJUNCTION || type == NodeType::NEGATION)
    markLinearBitwise();
  else if (type == NodeType::SUM)
    markLinearSum();
  else if (type == NodeType::PRODUCT)
    markLinearProduct();
  else if (type == NodeType::POWER)
    markLinearPower();
  else if (type == NodeType::VARIABLE)
    markLinearVariable();
  else if (type == NodeType::CONSTANT)
    markLinearConstant();

  reorderAndDetermineLinearEnd();
}

void Node::markLinearBitwise() {
  for (auto &c : children) {
    if (c->state != NodeState::BITWISE) {
      state = NodeState::MIXED;
      return;
    }
  }
  state = NodeState::BITWISE;
}

void Node::markLinearSum() {
  state = NodeState::UNKNOWN;
  for (auto &c : children) {
    if (c->state == NodeState::MIXED) {
      state = NodeState::MIXED;
      return;
    } else if (c->state == NodeState::NONLINEAR)
      state = NodeState::NONLINEAR;
  }
  if (state != NodeState::NONLINEAR)
    state = NodeState::LINEAR;
}

void Node::markLinearProduct() {
  if (children.size() < 2) {
    state = children[0]->state;
    return;
  }
  for (auto &c : children) {
    if (c->state == NodeState::MIXED) {
      state = NodeState::MIXED;
      return;
    }
  }
  if (children.size() > 2) {
    state = NodeState::NONLINEAR;
  } else if (children[0]->type == NodeType::CONSTANT && children[1]->isLinear()) {
    state = NodeState::LINEAR;
  } else if (children[1]->type == NodeType::CONSTANT && children[0]->isLinear()) {
    state = NodeState::LINEAR;
  } else {
    state = NodeState::NONLINEAR;
  }
}

void Node::markLinearPower() {
  for (auto &c : children) {
    if (c->state == NodeState::MIXED) {
      state = NodeState::MIXED;
      return;
    }
  }
  state = NodeState::NONLINEAR;
}

void Node::markLinearVariable() { state = NodeState::BITWISE; }

void Node::markLinearConstant() {
  if (isConstant(0) || isConstant(-1))
    state = NodeState::BITWISE;
  else
    state = NodeState::LINEAR;
}

void Node::reorderAndDetermineLinearEnd() {
  linearEnd = 0;
  if (type == NodeType::POWER)
    return;
  if (state != NodeState::NONLINEAR && state != NodeState::MIXED) {
    linearEnd = static_cast<int>(children.size());
    return;
  }
  if (type == NodeType::PRODUCT) {
    reorderAndDetermineLinearEndProduct();
    return;
  }
  bool bitwise = (type == NodeType::CONJUNCTION ||
                  type == NodeType::EXCL_DISJUNCTION ||
                  type == NodeType::INCL_DISJUNCTION || type == NodeType::NEGATION);
  for (int i = 0; i < static_cast<int>(children.size()); ++i) {
    auto child = children[i];
    if (child->state == NodeState::BITWISE ||
        (!bitwise && child->state == NodeState::LINEAR)) {
      if (linearEnd < i) {
        children.erase(children.begin() + i);
        children.insert(children.begin() + linearEnd, child);
      }
      linearEnd += 1;
    }
  }
}

void Node::reorderAndDetermineLinearEndProduct() {
  if (children[0]->type != NodeType::CONSTANT)
    return;
  linearEnd = 1;
  for (int i = 1; i < static_cast<int>(children.size()); ++i) {
    auto child = children[i];
    if (child->state != NodeState::NONLINEAR && child->state != NodeState::MIXED) {
      if (linearEnd < i) {
        children.erase(children.begin() + i);
        children.insert(children.begin() + linearEnd, child);
      }
      linearEnd += 1;
      if (linearEnd == 2)
        return;
    }
  }
}

// ===================================================== Phase 2: counting
int Node::countNodes(const std::vector<NodeType> *typeList) const {
  int cnt = 0;
  for (const auto &child : children)
    cnt += child->countNodes(typeList);
  if (typeList == nullptr ||
      std::find(typeList->begin(), typeList->end(), type) != typeList->end())
    cnt += 1;
  return cnt;
}

int Node::computeAlternation(bool *parentBitwise) const {
  if (type == NodeType::VARIABLE)
    return 0;
  if (type == NodeType::CONSTANT)
    return (parentBitwise != nullptr && *parentBitwise) ? 1 : 0;

  bool bitw = isBitwiseOp();
  int cnt = (parentBitwise != nullptr && *parentBitwise != bitw) ? 1 : 0;
  for (auto &child : children)
    cnt += child->computeAlternation(&bitw);
  return cnt;
}

int Node::computeAlternationLinear(bool hasParent) const {
  if (type == NodeType::SUM || type == NodeType::PRODUCT) {
    int cnt = 0;
    for (const auto &child : children)
      cnt += child->computeAlternationLinear(true);
    return cnt;
  }
  if (!hasParent)
    return 0;
  return (type != NodeType::VARIABLE && type != NodeType::CONSTANT) ? 1 : 0;
}

int Node::countTermsLinear() {
  if (type == NodeType::SUM) {
    int t = 0;
    for (auto &child : children)
      t += child->countTermsLinear();
    return t;
  }
  if (type == NodeType::PRODUCT) {
    if (children[0]->type == NodeType::CONSTANT)
      return children[1]->countTermsLinear();
    return children[0]->countTermsLinear();
  }
  return 1;
}

// ===================================================== Phase 2: ordering
void Node::sort() {
  for (auto &c : children)
    c->sort();
  reorderVariables();
}

void Node::reorderVariables() {
  if (typeRank(type) < typeRank(NodeType::PRODUCT))
    return;
  if (children.size() <= 1)
    return;
  std::sort(children.begin(), children.end(),
            [](const std::shared_ptr<Node> &a, const std::shared_ptr<Node> &b) {
              return *a < *b;
            });
}

bool Node::operator<(const Node &other) const {
  if (type == NodeType::CONSTANT)
    return true;
  if (other.type == NodeType::CONSTANT)
    return false;

  std::string vn1 = getExtendedVariable();
  std::string vn2 = other.getExtendedVariable();
  if (!vn1.empty()) {
    if (vn2.empty())
      return true;
    if (vn1 != vn2)
      return vn1 < vn2;
    return type == NodeType::VARIABLE;
  }

  if (!vn2.empty())
    return false;

  if (typeRank(type) != typeRank(other.type))
    return typeRank(type) < typeRank(other.type);
  return children.size() < other.children.size();
}

std::string Node::getExtendedVariable() const {
  if (type == NodeType::VARIABLE)
    return vname;
  if (type == NodeType::NEGATION) {
    if (children[0]->type == NodeType::VARIABLE)
      return children[0]->vname;
    return "";
  }
  if (type == NodeType::PRODUCT) {
    if (children.size() == 2 && children[0]->type == NodeType::CONSTANT &&
        children[1]->type == NodeType::VARIABLE)
      return children[1]->vname;
    return "";
  }
  return "";
}

// ===================================================== Phase 2: multiply
void Node::multiply(int64_t factor) {
  if (MBAOps::reduce(static_cast<uint64_t>(factor - 1), bitCount) == 0)
    return;

  if (type == NodeType::CONSTANT) {
    constant = getReducedConstant(constant * MBAOps::fromSigned(factor));
    return;
  }

  if (type == NodeType::SUM) {
    for (auto &child : children)
      child->multiply(factor);
    return;
  }

  if (type == NodeType::PRODUCT) {
    if (children[0]->type == NodeType::PRODUCT) {
      auto first = children[0];
      std::vector<std::shared_ptr<Node>> newChildren;
      newChildren.push_back(first->children[0]);
      for (size_t i = 1; i < first->children.size(); ++i)
        newChildren.push_back(first->children[i]);
      for (size_t i = 1; i < children.size(); ++i)
        newChildren.push_back(children[i]);
      children = std::move(newChildren);
    }

    if (children[0]->type == NodeType::CONSTANT) {
      children[0]->multiply(factor);
      if (children[0]->isConstant(1)) {
        children.erase(children.begin());
        if (children.size() == 1)
          copy(*children[0]);
      } else if (children[0]->isConstant(0)) {
        copy(*children[0]);
      }
      return;
    }

    children.insert(children.begin(), newConstantNode(factor));
    return;
  }

  auto fac = newConstantNode(factor);
  auto node = newNode(NodeType::CONSTANT);
  node->copy(*this);
  auto prod = newNodeWithChildren(NodeType::PRODUCT, {fac, node});
  copy(*prod);
}

void Node::multiplyByMinusOne() { multiply(-1); }

// Mirrors node.py are_all_children_contained.
bool areAllChildrenContained(const std::vector<std::shared_ptr<Node>> &l1,
                             const std::vector<std::shared_ptr<Node>> &l2) {
  std::vector<size_t> oIndices;
  for (size_t i = 0; i < l2.size(); ++i)
    oIndices.push_back(i);
  for (auto &child : l1) {
    bool found = false;
    for (auto it = oIndices.begin(); it != oIndices.end(); ++it) {
      if (child->equals(*l2[*it])) {
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

// Mirrors node.py copy / __copy_all / get_copy / __get_shallow_copy.
void Node::copy(const Node &node) {
  // Save every field before touching `children`: assigning `children` can
  // release the last shared_ptr to `node` (when `node` is one of our own
  // children), which would make reading `node` afterwards a use-after-free.
  // Python keeps `node` alive via the argument reference; mirror that here.
  NodeType t = node.type;
  NodeState s = node.state;
  std::vector<std::shared_ptr<Node>> ch = node.children;
  std::string vn = node.vname;
  int vi = node.vidx;
  MBAValue c = node.constant;

  type = t;
  state = s;
  children = ch; // alias (shared children), like Python
  vname = vn;
  vidx = vi;
  constant = c;
}

void Node::copyAll(const Node &node) {
  type = node.type;
  state = node.state;
  children.clear();
  vname = node.vname;
  vidx = node.vidx;
  constant = node.constant;
  for (auto &child : node.children)
    children.push_back(child->getCopy());
}

shared_ptr<Node> Node::getCopy() const {
  auto n = newNode(type);
  n->state = state;
  n->vname = vname;
  n->vidx = vidx;
  n->constant = constant;
  for (auto &child : children)
    n->children.push_back(child->getCopy());
  return n;
}

shared_ptr<Node> Node::getShallowCopy() const {
  auto n = newNode(type);
  n->state = state;
  n->vname = vname;
  n->vidx = vidx;
  n->constant = constant;
  n->children = children; // copy the list (shared child pointers)
  return n;
}

} // namespace MBA
} // namespace LSiMBA
