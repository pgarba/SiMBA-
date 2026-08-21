// GAMBA native C++ port — node type / state enums.
// Mirrors external/GAMBA/src/utils/node.py (NodeType, NodeState).
#ifndef MBA_NODETYPE_H
#define MBA_NODETYPE_H

#include <cstdint>

namespace LSiMBA {
namespace MBA {

// The type of a node representing a subexpression.
enum class NodeType : std::uint8_t {
  CONSTANT = 0,
  VARIABLE = 1,
  POWER = 2,
  NEGATION = 3,
  PRODUCT = 4,
  SUM = 5,
  CONJUNCTION = 6,
  EXCL_DISJUNCTION = 7,
  INCL_DISJUNCTION = 8,
};

// Additional information on a node.
enum class NodeState : std::uint8_t {
  UNKNOWN = 0,
  BITWISE = 1,
  LINEAR = 2,
  NONLINEAR = 3,
  MIXED = 4,
};

// Operator characters for the bitwise/arithmetic binary node types.
inline char OpChar(NodeType t) {
  switch (t) {
  case NodeType::POWER:
    return '*';   // rendered as "**"
  case NodeType::NEGATION:
    return '~';
  case NodeType::PRODUCT:
    return '*';
  case NodeType::SUM:
    return '+';   // rendered with +/- per term
  case NodeType::CONJUNCTION:
    return '&';
  case NodeType::EXCL_DISJUNCTION:
    return '^';
  case NodeType::INCL_DISJUNCTION:
    return '|';
  default:
    return 0;
  }
}

} // namespace MBA
} // namespace LSiMBA

#endif // MBA_NODETYPE_H
