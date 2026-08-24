// MSiMBA — Constant substitution for the 1-bit shortcut.
// Port of external/MSiMBA/Mba.Common/MSiMBA/ConstantSubstituter.cs.
//
// Idea: substitute constants inside bitwise operands with temporary
// variables, run the 1-bit SiMBA solver on the (now linear) expression,
// then back-substitute the constants.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Node.h"

namespace LSiMBA {
namespace MBA {

class ConstantSubstituter {
public:
  // Collect constants inside bitwise operands (AND, OR, XOR).
  // Returns a map of constant → list of (parent node, child index).
  struct UserEntry {
    std::shared_ptr<Node> parent;
    int childIndex;
  };
  using UserMapping = std::unordered_map<int64_t, std::vector<UserEntry>>;

  // Apply constant substitution. Returns the substituted AST and the
  // mapping from constant → temp variable name. Returns nullptr if
  // there are too many constants (>10) or no constants to substitute.
  static std::pair<std::shared_ptr<Node>, std::unordered_map<int64_t, std::string>>
  apply(const std::shared_ptr<Node> &ast,
        const std::unordered_set<std::string> &existingVars);

  // Back-substitute: replace temp variables with original constants.
  static std::shared_ptr<Node>
  applyBackSubstitution(const std::shared_ptr<Node> &ast,
                        const std::unordered_map<int64_t, std::string> &substMapping);

private:
  static void collect(const std::shared_ptr<Node> &node, UserMapping &userMapping,
                      bool inBitwise, bool &atLimit);
  static void add(UserMapping &userMapping, int64_t constant,
                  const std::shared_ptr<Node> &parent, int childIndex);
};

} // namespace MBA
} // namespace LSiMBA
