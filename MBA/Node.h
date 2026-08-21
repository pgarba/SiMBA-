// GAMBA native C++ port — abstract syntax tree node.
// Mirrors external/GAMBA/src/utils/node.py (class Node).
//
// Ownership: children are std::shared_ptr<Node>.
//   * copy(node)         -> aliases node's children list (shared children),
//                           matching Python `self.children = node.children`.
//   * getCopy()          -> deep copy into a new node.
//   * getShallowCopy()   -> new node sharing the same child pointers.
#ifndef MBA_NODE_H
#define MBA_NODE_H

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "Batch.h"
#include "MBAValue.h"
#include "NodeType.h"

namespace LSiMBA {
namespace MBA {

class Node {
public:
  // Fields (mirroring the Python node's public attributes).
  NodeType type = NodeType::CONSTANT;
  std::vector<std::shared_ptr<Node>> children;
  std::string vname = "";
  int vidx = -1;
  MBAValue constant = MBAValue(128, 0);
  NodeState state = NodeState::UNKNOWN;
  int bitCount = 64;
  bool modRed = false;
  int linearEnd = 0;
  int MAX_IT = 10;

  Node(NodeType t, int bitCount, bool modRed = false)
      : type(t), bitCount(bitCount), modRed(modRed) {}

  // Rank used for the Python `type > NodeType.X` precedence comparisons.
  static int typeRank(NodeType t) { return static_cast<int>(t); }

  // ---------------------------------------------------------------- string
  std::string toString(bool withParentheses = false, int end = -1,
                       const std::vector<std::string> *varNames = nullptr);

  std::string partToString(int end) { return toString(false, end); }

  // ---------------------------------------------------------- constants
  std::uint64_t power(std::uint64_t b, std::uint64_t e) {
    return MBAOps::power(b, e, bitCount);
  }

  // Reduce c modulo 2^bitCount; if modRed is false and c > half modulus,
  // subtract modulus to keep a minimal absolute value.
  MBAValue getReducedConstant(const MBAValue &c) {
    if (modRed) {
      return c.zextOrTrunc(bitCount >= 64 ? 64 : bitCount).zext(128);
    }
    std::uint64_t r = MBAOps::toLow64(c, bitCount);
    std::int64_t s = MBAOps::reduceSigned(static_cast<std::int64_t>(r), bitCount);
    return MBAOps::fromSigned(s);
  }

  void reduceConstant() { constant = getReducedConstant(constant); }

  void setAndReduceConstant(std::int64_t c) {
    constant = getReducedConstant(MBAOps::fromSigned(c));
  }

  bool isConstant(std::int64_t value) {
    return type == NodeType::CONSTANT && constant == MBAOps::fromSigned(value);
  }

  // --------------------------------------------------------- variables
  void collectAndEnumerateVariables(std::vector<std::string> &variables);
  void collectVariables(std::vector<std::string> &variables);
  void enumerateVariables(const std::vector<std::string> &variables);
  int getMaxVname(const std::string &start, const std::string &end);

  // ------------------------------------------------------------- eval
  std::uint64_t eval(const std::vector<std::uint64_t> &X);
  std::uint64_t applyBinop(std::uint64_t x, std::uint64_t y);

  // ------------------------------------------------------------- queries
  bool hasNonlinearChild();
  bool isLinear() const;
  bool isBitwiseOp() const;
  bool isBitwiseBinop() const;
  bool isArithmOp() const;

  // Mirrors node.py Node.__lt__ (ordering used for factorization).
  bool lessThan(const Node &other) const;

  // ------------------------------------------------------------- equality
  bool equals(const Node &other) const;
  bool equalsNegated(const Node &other) const;
  bool equalsRewritingBitwise(const Node &other) const;
  bool equalsRewritingBitwiseAsymm(const Node &other) const;
  std::shared_ptr<Node> getOptTransformedNegated() const;
  std::shared_ptr<Node> getOptTransformedNegatedSum() const;
  std::shared_ptr<Node> getOptTransformedNegatedProduct() const;
  std::shared_ptr<Node> getOptNegativeTransformedNegated() const;

  // ------------------------------------------------------------- mark linear
  void markLinear(bool restrictedScope = false);
  void markLinearBitwise();
  void markLinearSum();
  void markLinearProduct();
  void markLinearPower();
  void markLinearVariable();
  void markLinearConstant();
  void reorderAndDetermineLinearEnd();
  void reorderAndDetermineLinearEndProduct();

  // ------------------------------------------------------------- counting
  int countNodes(const std::vector<NodeType> *typeList = nullptr) const;
  int computeAlternation(bool *parentBitwise = nullptr) const;
  int computeAlternationLinear(bool hasParent = false) const;
  int countTermsLinear();

  // ------------------------------------------------------------- ordering
  void sort();
  void reorderVariables();
  bool operator<(const Node &other) const;
  std::string getExtendedVariable() const;

  // ------------------------------------------------------------- multiply
  void multiply(std::int64_t factor);
  void multiplyByMinusOne();

  // ------------------------------------------------------------- copy
  void copy(const Node &node);
  void copyAll(const Node &node);
  std::shared_ptr<Node> getCopy() const;
  std::shared_ptr<Node> getShallowCopy() const;

  // --------------------------------------------------------- factories
  std::shared_ptr<Node> newNode(NodeType t) const {
    return std::make_shared<Node>(t, bitCount, modRed);
  }
  std::shared_ptr<Node> newConstantNode(std::int64_t constant) const {
    auto node = newNode(NodeType::CONSTANT);
    node->constant = MBAOps::fromSigned(constant);
    node->reduceConstant();
    return node;
  }
  std::shared_ptr<Node> newVariableNode(const std::string &vname) const {
    auto node = newNode(NodeType::VARIABLE);
    node->vname = vname;
    return node;
  }
  std::shared_ptr<Node>
  newNodeWithChildren(NodeType t, std::vector<std::shared_ptr<Node>> children) const {
    auto node = newNode(t);
    node->children = std::move(children);
    return node;
  }

  // Refinement (GAMBA port) — member declarations provided by batch headers.
#include "RefineA.h"
#include "RefineB.h"
#include "RefineC.h"
#include "RefineD.h"
#include "Expand.h"
#include "Substitute.h"
};

// Returns true iff all children contained in l1 are also contained in l2.
// Mirrors node.py are_all_children_contained.
bool areAllChildrenContained(const std::vector<std::shared_ptr<Node>> &l1,
                             const std::vector<std::shared_ptr<Node>> &l2);

// Returns true iff both lists have the same length and every child of l1 also
// appears in l2. Mirrors node.py do_children_match.
bool doChildrenMatch(const std::vector<std::shared_ptr<Node>> &l1,
                     const std::vector<std::shared_ptr<Node>> &l2);

} // namespace MBA
} // namespace LSiMBA

#endif // MBA_NODE_H
