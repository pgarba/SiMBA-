// MSiMBA — Constant substitution implementation.
#include "ConstantSubstituter.h"

#include <algorithm>

namespace LSiMBA {
namespace MBA {

// ================================================================ collect

void ConstantSubstituter::collect(const std::shared_ptr<Node> &node,
                                   UserMapping &userMapping, bool inBitwise,
                                   bool &atLimit) {
  if (atLimit)
    return;
  if (userMapping.size() >= 10) {
    atLimit = true;
    return;
  }

  auto kind = node->type;

  if (kind == NodeType::CONJUNCTION || kind == NodeType::INCL_DISJUNCTION || kind == NodeType::EXCL_DISJUNCTION) {
    for (size_t i = 0; i < node->children.size(); i++) {
      auto &child = node->children[i];
      if (child->type == NodeType::CONSTANT) {
        add(userMapping, child->constant.getSExtValue(), node, static_cast<int>(i));
      } else {
        collect(child, userMapping, true, atLimit);
      }
    }
  } else if (kind == NodeType::NEGATION && inBitwise) {
    auto &single = node->children[0];
    if (single->type == NodeType::CONSTANT) {
      add(userMapping, single->constant.getSExtValue(), node, 0);
    } else {
      collect(single, userMapping, inBitwise, atLimit);
    }
  } else if (kind == NodeType::VARIABLE || kind == NodeType::CONSTANT) {
    return;
  } else {
    for (auto &child : node->children)
      collect(child, userMapping, inBitwise, atLimit);
  }
}

void ConstantSubstituter::add(UserMapping &userMapping, int64_t constant,
                               const std::shared_ptr<Node> &parent, int childIndex) {
  auto it = userMapping.find(constant);
  if (it != userMapping.end()) {
    it->second.push_back({parent, childIndex});
  } else {
    userMapping[constant] = {{parent, childIndex}};
  }
}

// ================================================================ apply

std::pair<std::shared_ptr<Node>, std::unordered_map<int64_t, std::string>>
ConstantSubstituter::apply(const std::shared_ptr<Node> &ast,
                            const std::unordered_set<std::string> &existingVars) {
  // Clone the AST since we're mutating it.
  auto cloned = ast->getCopy();

  // Collect all bitwise constants.
  UserMapping userMapping;
  bool atLimit = false;
  collect(cloned, userMapping, false, atLimit);
  if (atLimit || userMapping.empty())
    return {nullptr, {}};

  // Substitute all unique constants with temporary variables.
  //
  // The name MUST be a single parser token: the substituted expression is
  // serialized and re-parsed (by the 1-bit SiMBA path), so a name with an
  // operator in it is silently rewritten. The old `um + to_string(constant)`
  // produced `um-1112` for negative constants, which re-parses as the
  // SUBTRACTION `um - 1112` — the 1-bit solver then works on a different
  // expression, back-substitution finds no `um-1112` variable, and the
  // result is not equivalent (it used to surface as the 16 negative-
  // constant canonical-test failures; tests/test_canonical.py).
  std::unordered_map<int64_t, std::string> substMapping;
  for (auto &[constant, users] : userMapping) {
    std::string name =
        constant < 0 ? ("um_n" + std::to_string(static_cast<uint64_t>(-constant)))
                     : ("um" + std::to_string(constant));
    if (existingVars.count(name))
      return {nullptr, {}}; // Name conflict.

    auto subst = std::make_shared<Node>(NodeType::VARIABLE, ast->bitCount);
    subst->vname = name;
    substMapping[constant] = name;

    for (auto &user : users) {
      user.parent->children[user.childIndex] = subst;
    }
  }

  return {cloned, substMapping};
}

// ================================================================ back-substitute

std::shared_ptr<Node>
ConstantSubstituter::applyBackSubstitution(const std::shared_ptr<Node> &ast,
                                            const std::unordered_map<int64_t, std::string> &substMapping) {
  // Build inverse mapping: temp var name → constant node.
  std::unordered_map<std::string, std::shared_ptr<Node>> inverseMapping;
  for (auto &[constant, name] : substMapping) {
    auto constNode = std::make_shared<Node>(NodeType::CONSTANT, ast->bitCount);
    constNode->constant = MBAOps::fromSigned(constant);
    inverseMapping[name] = constNode;
  }

  // Walk the AST and replace temp variables with constants.
  std::function<std::shared_ptr<Node>(const std::shared_ptr<Node> &)> replace =
      [&](const std::shared_ptr<Node> &node) -> std::shared_ptr<Node> {
    if (node->type == NodeType::VARIABLE && inverseMapping.count(node->vname)) {
      return inverseMapping[node->vname];
    }
    auto result = std::make_shared<Node>(node->type, node->bitCount);
    result->constant = node->constant;
    result->vname = node->vname;
    for (auto &child : node->children) {
      result->children.push_back(replace(child));
    }
    return result;
  };

  return replace(ast);
}

} // namespace MBA
} // namespace LSiMBA
