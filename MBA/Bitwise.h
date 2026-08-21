// GAMBA native C++ port — Bitwise AST.
// Mirrors external/GAMBA/src/bitwise-factory/utils/bitwise.py.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace LSiMBA {
namespace MBA {

// The type of a node representing a bitwise (sub-)expression.
enum class BitwiseType {
  TRUE = 0,
  VARIABLE = 1,
  CONJUNCTION = 2,
  EXCL_DISJUNCTION = 3,
  INCL_DISJUNCTION = 4,
};

// An AST structure representing a bitwise (sub-)expression.
class Bitwise {
 public:
  Bitwise(BitwiseType bType, bool negated = false, int vidx = -1);

  void addChild(std::shared_ptr<Bitwise> child);
  void addVariable(int vidx, bool negated = false);
  int childCount() const { return static_cast<int>(children.size()); }
  std::shared_ptr<Bitwise> firstChild() const { return children[0]; }

  std::string toString(const std::vector<std::string> &variables = {},
                      bool withParentheses = false) const;

  bool equals(const Bitwise &other, bool negated = false) const;

  void refine();

 private:
  BitwiseType type;
  int vidx;
  bool negated;
  std::vector<std::shared_ptr<Bitwise>> children;

  std::string opToString() const;
  bool areAllChildrenContained(const Bitwise &other) const;
  void pullUpChild();
  void copy(const Bitwise &node);
  std::shared_ptr<Bitwise> getCopy() const;
  bool refineStep();
  bool checkInsertXor();
  bool tryInsertXor(int i, int j);
  bool checkFlipNegation();
  bool doAllChildrenHaveType(BitwiseType t) const;
  bool checkExtract();
  std::shared_ptr<Bitwise> tryExtract();
  std::shared_ptr<Bitwise> getCommonChild();
  bool hasChildInRemainingChildren(const Bitwise &node);
  bool hasChild(const Bitwise &node) const;
  void removeChild(const Bitwise &node);
};

} // namespace MBA
} // namespace LSiMBA
