// GAMBA native C++ port — general (nonlinear) MBA simplifier.
// Mirrors external/GAMBA/src/simplify_general.py.
#include "GeneralSimplifier.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>

#include "Verify.h"

namespace LSiMBA {
namespace MBA {

// (a*b) mod 2^bitCount for signed 64-bit values, via the 128-bit MBAValue.
static uint64_t mulMod128(int64_t a, int64_t b, int bitCount) {
  MBAValue p = MBAOps::fromSigned(a) * MBAOps::fromSigned(b);
  return p.zextOrTrunc(bitCount >= 64 ? 64 : bitCount).getZExtValue();
}

GeneralSimplifier::GeneralSimplifier(int bitCount, bool modRed, int verifBitCount,
                                     int timeoutSec)
    : bitCount(bitCount),
      modulus(bitCount >= 64 ? ~0ULL : (1ULL << bitCount)),
      modRed(modRed),
      verifBitCount(verifBitCount),
      timeoutSec(timeoutSec) {
  const char *p = std::getenv("MBASIMBA_PERF");
  perf.enabled = (p != nullptr && p[0] == '1');
  // W1: per-expression linear-solver memo. Shipped ON (validated
  // byte-identical on the full dataset); MBASIMBA_LINMEMO=0 disables it.
  const char *m = std::getenv("MBASIMBA_LINMEMO");
  linMemoEnabled = (m == nullptr || m[0] != '0');
  // Phase 2: direct single-pass first (CoBRA-style). Default on.
  const char *d = std::getenv("MBASIMBA_DIRECT");
  directEnabled = (d == nullptr || d[0] != '0');
  // Phase-1 substitution budget (see header). Default: 150ms cumulative
  // substitution time (the controlling knob), no subset cap. 0 disables
  // (historical unbounded behavior). Measured on the 10-variable obfuscatorx
  // cases the substitution size gain only materializes after ~20s of
  // thrashing, so a tight time bound keeps the tail bounded with no quality
  // loss on the datasets where substitution gives early wins (e.g.
  // permutation64 is unaffected).
  substBudgetMs = 150.0;
  substBudgetSubsets = 0;
  if (const char *b = std::getenv("MBASIMBA_SUBST_BUDGET_MS")) {
    try {
      substBudgetMs = std::stod(b);
    } catch (...) {
    }
  }
  if (const char *s = std::getenv("MBASIMBA_SUBST_MAX_SUBSETS")) {
    try {
      substBudgetSubsets = std::stol(s);
    } catch (...) {
    }
  }
}

// A3: lazily-created zero constant node (equivalent to parse("0", ...)).
std::shared_ptr<Node> GeneralSimplifier::getZero() {
  if (zeroNode == nullptr) {
    zeroNode = std::make_shared<Node>(NodeType::CONSTANT, bitCount, modRed);
    zeroNode->constant = MBAOps::fromSigned(0);
    zeroNode->reduceConstant();
  }
  return zeroNode;
}

// Returns the number of variables occurring in the given expression.
int GeneralSimplifier::getVariableCount(const std::string &expr) {
  auto tree = parse(expr, bitCount, modRed);
  if (tree == nullptr)
    return -1;

  collectAndEnumerateVariables(tree);
  return vnumber;
}

// Get the internal name of the variable with given index.
std::string GeneralSimplifier::getVname(int i) const {
  return "Y[" + std::to_string(i) + "]";
}

// Reduces the given number modulo modulus (Python mod_red semantics: always
// non-negative).
int64_t GeneralSimplifier::modRedInt(int64_t n) const {
  if (bitCount >= 64)
    return n;
  return static_cast<int64_t>(static_cast<uint64_t>(n) & (modulus - 1));
}

// Find all variables occuring in the given tree, store them in a list and
// enumerate the tree's variable nodes accordingly.
void GeneralSimplifier::collectAndEnumerateVariables(const std::shared_ptr<Node> &tree) {
  variables.clear();
  tree->collectAndEnumerateVariables(variables);
  vnumber = static_cast<int>(variables.size());
}

// Get the vector storing results of expression evaluation for all truth value
// combinations.
std::vector<int64_t> GeneralSimplifier::getResultVector(const std::shared_ptr<Node> &node) const {
  std::vector<int64_t> resultVector;
  int total = 1 << vnumber;
  for (int i = 0; i < total; ++i) {
    int n = i;
    std::vector<uint64_t> par;
    for (int j = 0; j < vnumber; ++j) {
      par.push_back(static_cast<uint64_t>(n & 1));
      n >>= 1;
    }
    resultVector.push_back(static_cast<int64_t>(node->eval(par)));
  }
  return resultVector;
}

// Returns a vector of the group sizes [1, 2, 4, ...]
std::vector<int> GeneralSimplifier::getGroupSizes() const {
  std::vector<int> gs = {1};
  for (int i = 1; i < vnumber; ++i)
    gs.push_back(2 * gs.back());
  return gs;
}

// Get all possible variable combinations, e.g.,
// [[0], [1], [2], [0, 1], [0, 2], [1, 2], [0, 1, 2]] for 3 variables.
std::vector<std::vector<int>> GeneralSimplifier::getVariableCombinations() const {
  std::vector<std::vector<int>> comb;
  for (int v = 0; v < vnumber; ++v)
    comb.push_back({v});
  int new_ = vnumber;

  for (int count = 1; count < vnumber; ++count) {
    int size = static_cast<int>(comb.size());
    int nnew = 0;
    for (int p = size - new_; p < size; ++p) {
      auto e = comb[p];
      for (int v = e.back() + 1; v < vnumber; ++v) {
        auto ne = e;
        ne.push_back(v);
        comb.push_back(ne);
        nnew += 1;
      }
    }
    new_ = nnew;
  }

  return comb;
}

// Returns the basis expression for the given index.
std::string GeneralSimplifier::getBasisExpression(int idx) const {
  if (idx == 0)
    return "1";

  std::string res = "";
  for (int v = 0; v < vnumber; ++v) {
    if ((idx & 1) == 1)
      res += variables[v] + "&";
    idx >>= 1;
  }

  res = res.substr(0, res.size() - 1);
  if (res.find('&') != std::string::npos)
    res = "(" + res + ")";
  return res;
}

// Returns true iff the variables at the given indices are all true for the
// given truth value.
bool GeneralSimplifier::areVariablesTrue(int n, const std::vector<int> &variables) const {
  int prev = 0;
  for (int v : variables) {
    n >>= (v - prev);
    prev = v;
    if ((n & 1) == 0)
      return false;
  }
  return true;
}

// Subtracts the given coefficient from the result vector entries.
void GeneralSimplifier::subtractCoefficient(std::vector<int64_t> &resultVector, int64_t coeff,
                                           int firstStart, const std::vector<int> &variables,
                                           int groupsize) {
  int period = 2 * groupsize;
  for (int start = firstStart; start < static_cast<int>(resultVector.size()); start += period) {
    for (int i = start; i < start + groupsize; ++i) {
      if (i != firstStart &&
          (variables.size() == 1 ||
           areVariablesTrue(i, std::vector<int>(variables.begin() + 1, variables.end())))) {
        resultVector[i] -= coeff;
      }
    }
  }
}

// Returns the linear combination for the given node.
std::vector<int64_t> GeneralSimplifier::getLinearCombination(const std::shared_ptr<Node> &node) {
  std::vector<int64_t> resultVector = getResultVector(node);
  int l = static_cast<int>(resultVector.size());

  int64_t constant = modRedInt(resultVector[0]);
  for (int i = 1; i < l; ++i)
    resultVector[i] -= constant;

  std::vector<std::vector<int>> combinations = getVariableCombinations();
  std::vector<int> groupsizes = getGroupSizes();
  for (auto &comb : combinations) {
    int index = 0;
    for (int v : comb)
      index += groupsizes[v];
    int64_t coeff = modRedInt(resultVector[index]);

    if (coeff == 0)
      continue;

    subtractCoefficient(resultVector, coeff, index, comb, groupsizes[comb[0]]);
  }

  return resultVector;
}

// Returns the linear combination for the given product node.
std::vector<uint64_t> GeneralSimplifier::getProductLinearCombination(
    const std::shared_ptr<Node> &node) {
  // assert: len(children) == 2 or (== 3 and children[0].type == CONSTANT)

  // The last two children.
  std::vector<std::vector<int64_t>> linCombs;
  linCombs.push_back(getLinearCombination(node->children[node->children.size() - 2]));
  linCombs.push_back(getLinearCombination(node->children[node->children.size() - 1]));

  std::vector<uint64_t> res((1 << (2 * vnumber - 1)) + (1 << (vnumber - 1)), 0);
  int baselen = 1 << vnumber;

  if (node->children.size() == 3 && node->children[0]->type == NodeType::CONSTANT) {
    int64_t c = static_cast<int64_t>(MBAOps::toLow64(node->children[0]->constant, bitCount));
    for (size_t i = 0; i < linCombs[0].size(); ++i)
      linCombs[0][i] = static_cast<int64_t>(mulMod128(linCombs[0][i], c, bitCount));
  }

  int idx = 0;
  for (int b = 0; b < baselen; ++b) {
    res[idx] = mulMod128(linCombs[0][b], linCombs[1][b], bitCount);
    idx += 1;

    for (int a = b + 1; a < baselen; ++a) {
      uint64_t p1 = mulMod128(linCombs[0][b], linCombs[1][a], bitCount);
      uint64_t p2 = mulMod128(linCombs[0][a], linCombs[1][b], bitCount);
      res[idx] = MBAOps::reduce(p1 + p2, bitCount);
      idx += 1;
    }
  }

  return res;
}

// Returns the linear combination for the given power node.
std::vector<uint64_t> GeneralSimplifier::getPowerLinearCombination(
    const std::shared_ptr<Node> &node) {
  // assert: len(children) == 2 or node.type == PRODUCT

  auto base = node->children[0];
  int64_t coeff = 1;
  if (node->type == NodeType::PRODUCT) {
    // assert: children[0] is CONSTANT, children[1] is POWER with exponent 2.
    base = node->children[1]->children[0];
    coeff = static_cast<int64_t>(MBAOps::toLow64(node->children[0]->constant, bitCount));
  }

  std::vector<int64_t> linComb = getLinearCombination(base);

  std::vector<uint64_t> res((1 << (2 * vnumber - 1)) + (1 << (vnumber - 1)), 0);
  int baselen = 1 << vnumber;

  int idx = 0;
  for (int b = 0; b < baselen; ++b) {
    res[idx] = mulMod128(linComb[b], linComb[b], bitCount);
    idx += 1;

    for (int a = b + 1; a < baselen; ++a) {
      uint64_t p = mulMod128(linComb[b], linComb[a], bitCount);
      res[idx] = MBAOps::reduce(2 * p, bitCount);
      idx += 1;
    }
  }

  if (coeff != 1) {
    for (size_t i = 0; i < res.size(); ++i)
      res[i] = mulMod128(static_cast<int64_t>(res[i]), coeff, bitCount);
  }

  return res;
}

// Try to simplify the nonlinear part of the given sum.
bool GeneralSimplifier::trySimplifySumNonlinearPart(const std::shared_ptr<Node> &node) {
  std::vector<int> indices = getIndicesOfSimpleNonlinearProductsInSum(node);
  if (indices.size() == 0 || indices.size() == 1)
    return false;

  collectAndEnumerateVariables(node);

  std::vector<uint64_t> res((1 << (2 * vnumber - 1)) + (1 << (vnumber - 1)), 0);
  for (int i : indices) {
    auto child = node->children[i];
    if (child->type == NodeType::PRODUCT && !child->hasNonlinearChild()) {
      auto p = getProductLinearCombination(child);
      for (size_t k = 0; k < res.size(); ++k)
        res[k] += p[k];
    } else {
      auto p = getPowerLinearCombination(child);
      for (size_t k = 0; k < res.size(); ++k)
        res[k] += p[k];
    }
  }
  for (size_t i = 0; i < res.size(); ++i)
    res[i] = MBAOps::reduce(res[i], bitCount);

  int baselen = 1 << vnumber;

  std::string simpl = "";
  int idx = 0;
  for (int b = 0; b < baselen; ++b) {
    if (res[idx] != 0) {
      if (res[idx] != 1)
        simpl += std::to_string(static_cast<int64_t>(res[idx])) + "*";
      simpl += getBasisExpression(b) + "**2+";
    }
    idx += 1;

    for (int a = b + 1; a < baselen; ++a) {
      if (res[idx] != 0) {
        if (res[idx] != 1)
          simpl += std::to_string(static_cast<int64_t>(res[idx])) + "*";
        simpl += getBasisExpression(b) + "*" + getBasisExpression(a) + "+";
      }
      idx += 1;
    }
  }

  if (!simpl.empty())
    simpl = simpl.substr(0, simpl.size() - 1);

  // Remove all but the first of the candidate children.
  std::vector<int> rest(indices.begin() + 1, indices.end());
  std::sort(rest.rbegin(), rest.rend());
  for (int i : rest)
    node->children.erase(node->children.begin() + i);

  if (!simpl.empty()) {
    auto parsed = parse(simpl, bitCount, modRed, true, true);
    if (parsed != nullptr) {
      node->children[indices[0]] = parsed;
    } else {
      node->children.erase(node->children.begin() + indices[0]);
    }
  } else {
    node->children.erase(node->children.begin() + indices[0]);
  }

  if (node->children.size() == 1) {
    node->copy(*node->children[0]);
  } else if (node->children.empty()) {
    node->copy(*getZero());
  }

  return true;
}

// Returns true iff the given node is a candidate for simplification in a sum.
bool GeneralSimplifier::isCandidateForSimplificationInSum(
    const std::shared_ptr<Node> &node) const {
  if (node->type != NodeType::PRODUCT && node->type != NodeType::POWER)
    return false;
  if (node->state != NodeState::NONLINEAR)
    return false;

  if (node->children.size() > 2) {
    if (node->children.size() > 3 || node->children[0]->type != NodeType::CONSTANT)
      return false;
  }

  if (node->type == NodeType::POWER) {
    if (node->children[1]->type != NodeType::CONSTANT || !node->children[1]->isConstant(2))
      return false;
    if (node->children[0]->state == NodeState::NONLINEAR ||
        node->children[0]->state == NodeState::MIXED)
      return false;
  } else {
    if (node->children[0]->type == NodeType::CONSTANT && node->children[1]->type == NodeType::POWER) {
      if (node->children.size() > 2)
        return false;
      return isCandidateForSimplificationInSum(node->children[1]);
    }
    if (node->hasNonlinearChild())
      return false;
  }

  return true;
}

// Get the indices of simple nonlinear products in the given sum.
std::vector<int> GeneralSimplifier::getIndicesOfSimpleNonlinearProductsInSum(
    const std::shared_ptr<Node> &node) {
  if (node->linearEnd >= static_cast<int>(node->children.size()) - 1)
    return {};

  std::vector<int> indices;
  for (int i = 0; i < static_cast<int>(node->children.size()); ++i)
    if (isCandidateForSimplificationInSum(node->children[i]))
      indices.push_back(i);

  return indices;
}

// Simplify the linear part of the given nonlinear subexpression.
bool GeneralSimplifier::simplifyNonlinearSubexpressionLinearPart(
    const std::shared_ptr<Node> &node) {
  std::string subexpr = node->partToString(node->linearEnd);
  std::string simpl = simplifyLinearMba(subexpr, bitCount, false, false, modRed);

  if (simpl == subexpr)
    return false;

  auto child = parse(simpl, bitCount, modRed, true, true);
  if (child == nullptr)
    return false;

  if (child->type == node->type) {
    // del node.children[:node.linearEnd]
    node->children.erase(node->children.begin(), node->children.begin() + node->linearEnd);
    // node.children = child.children + node.children
    std::vector<std::shared_ptr<Node>> newChildren = child->children;
    for (auto &c : node->children)
      newChildren.push_back(c);
    node->children = std::move(newChildren);
    node->linearEnd = static_cast<int>(child->children.size());
  } else if (simpl == "0") {
    // assert: node.type != POWER
    if (node->type == NodeType::SUM || node->type == NodeType::INCL_DISJUNCTION ||
        node->type == NodeType::EXCL_DISJUNCTION) {
      node->children.erase(node->children.begin(), node->children.begin() + node->linearEnd);
      node->linearEnd = 0;
      if (node->children.size() == 1)
        node->copy(*node->children[0]);
    } else {
      // assert: node.type in [PRODUCT, CONJUNCTION]
      node->copy(*getZero());
    }
  } else {
    // del node.children[1:node.linearEnd]
    node->children.erase(node->children.begin() + 1, node->children.begin() + node->linearEnd);
    node->children[0] = child;
    node->linearEnd = 1;
  }

  return true;
}

// Refactor the given node.
bool GeneralSimplifier::refactor(const std::shared_ptr<Node> &node) {
  if (perf.enabled)
    perf.refactorCalls++;

  // A1: skip the expensive expand+factorize rebuild when the node is
  // byte-identical to a previous no-change refactor. expand+factorize are
  // idempotent, so re-running them on such a node is a guaranteed no-op.
  uintptr_t key = reinterpret_cast<uintptr_t>(node.get());
  std::string cur = node->toString();
  auto it = noChangeFingerprint.find(key);
  if (it != noChangeFingerprint.end() && it->second == cur) {
    if (perf.enabled)
      perf.refactorSkips++;
    return false;
  }

  std::chrono::steady_clock::time_point t0;
  if (perf.enabled)
    t0 = std::chrono::steady_clock::now();

  node->expand(true);
  node->markLinear();
  node->factorizeSums(true);
  node->markLinear();

  if (perf.enabled)
    perf.tRefactor +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

  std::string after = node->toString();
  bool changed = after != cur;
  if (changed)
    noChangeFingerprint.erase(key);  // node changed; reset its fingerprint
  else
    noChangeFingerprint[key] = cur;  // remember the no-change fingerprint
  return changed;
}

// Perform one step of the nonlinear subexpression simplification.
bool GeneralSimplifier::simplifyNonlinearSubexpressionStep(const std::shared_ptr<Node> &node,
                                                          const std::shared_ptr<Node> &parent,
                                                          bool noRefactor, bool noSubst) {
  // assert: !node.isLinear()
  bool changed = false;

  if (node->linearEnd > 0) {
    std::chrono::steady_clock::time_point t0;
    if (perf.enabled)
      t0 = std::chrono::steady_clock::now();
    bool ch = simplifyNonlinearSubexpressionLinearPart(node);
    if (perf.enabled)
      perf.tLinear +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (ch)
      changed = true;
  }

  if (!noRefactor) {
    bool ch = refactor(node);
    if (ch) {
      for (auto &child : node->children)
        simplifySubexpression(child, node, noRefactor, noSubst);
      node->refine();
      node->markLinear();
    }
  }

  if (!noSubst && node->type != NodeType::NEGATION) {
    std::chrono::steady_clock::time_point t0;
    if (perf.enabled)
      t0 = std::chrono::steady_clock::now();
    if (simplifyViaSubstitution(node))
      changed = true;
    if (perf.enabled)
      perf.tSubst +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  }

  return changed;
}

// Simplify the given nonlinear subexpression.
bool GeneralSimplifier::simplifyNonlinearSubexpression(const std::shared_ptr<Node> &node,
                                                      const std::shared_ptr<Node> &parent,
                                                      bool noRefactor, bool noSubst) {
  std::set<std::string> prev;
  prev.insert(node->toString());
  bool changed = false;

  if (node->type == NodeType::SUM && trySimplifySumNonlinearPart(node)) {
    node->refine();
    node->markLinear();
    if (node->isLinear()) {
      simplifyLinearSubexpression(node);
      return true;
    }
    changed = true;
    for (auto &child : node->children)
      simplifySubexpression(child, node);
  }

  for (int i = 0; i < maxIt; ++i) {
    if (std::chrono::steady_clock::now() > deadline) {
      if (perf.enabled)
        perf.loopDeadline++;
      break;
    }
    if (perf.enabled)
      perf.iters++;

    bool ch = simplifyNonlinearSubexpressionStep(node, parent, noRefactor, noSubst);
    if (ch)
      changed = true;

    if (node->isLinear()) {
      simplifyLinearSubexpression(node);
      return true;
    }

    if (!ch) {
      if (perf.enabled)
        perf.loopNoChange++;
      break;
    }

    std::string s = node->toString();
    if (prev.count(s)) {
      if (perf.enabled)
        perf.loopCycle++;
      break;
    }
    if (perf.enabled && i == maxIt - 1)
      perf.loopMaxIt++;
    prev.insert(s);
  }

  return changed;
}

// Simplify the given linear subexpression.
bool GeneralSimplifier::simplifyLinearSubexpression(const std::shared_ptr<Node> &node) {
  std::string subexpr = node->toString();
  if (perf.enabled)
    perf.linearSubDupCalls += (linearSeen.count(subexpr) != 0) ? 1 : 0;
  if (perf.enabled)
    linearSeen.insert(subexpr);

  // W1: memoize the string-level linear solver (90% of calls on this
  // dataset re-submit an already-seen subexpression). A cached value equal
  // to the key is a solver fixed point: the solver would return unchanged,
  // so skip it and the comparison entirely.
  if (linMemoEnabled) {
    auto it = linearSimplifyMemo.find(subexpr);
    if (it != linearSimplifyMemo.end()) {
      if (perf.enabled)
        perf.linearCacheHits++;
      if (it->second == subexpr)
        return false;
      auto parsed = parse(it->second, bitCount, modRed, true, true);
      if (parsed != nullptr)
        node->copy(*parsed);
      return true;
    }
  }

  std::chrono::steady_clock::time_point ts;
  if (perf.enabled)
    ts = std::chrono::steady_clock::now();
  std::string simpl = simplifyLinearMba(subexpr, bitCount, false, false, modRed);
  if (linMemoEnabled)
    linearSimplifyMemo.emplace(subexpr, simpl);
  if (perf.enabled) {
    perf.tLinearMba +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();
    perf.linearSubCalls++;
  }
  bool changed = simpl != subexpr;

  if (changed) {
    if (perf.enabled)
      ts = std::chrono::steady_clock::now();
    auto parsed = parse(simpl, bitCount, modRed, true, true);
    if (parsed != nullptr)
      node->copy(*parsed);
    if (perf.enabled) {
      perf.tLinearParse +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();
      perf.linearSubChanged++;
    }
  }
  if (perf.enabled)
    perf.tLinearSub +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();

  return changed;
}

// Collect all nodes that can be used for substitution.
std::vector<std::shared_ptr<Node>> GeneralSimplifier::collectNodesForSubstitution(
    const std::shared_ptr<Node> &root) {
  std::vector<std::shared_ptr<Node>> nodes;
  while (true) {
    auto node = root->getNodeForSubstitution(nodes);
    if (node == nullptr)
      break;
    nodes.push_back(node);
  }
  return nodes;
}

// Get the simplification of the given node via substitution of the given
// nodes.
std::shared_ptr<Node> GeneralSimplifier::getSimplViaSubstitutionOfNodes(
    const std::shared_ptr<Node> &node, const std::vector<std::shared_ptr<Node>> &nodes,
    bool onlyFullMatch) {
  std::chrono::steady_clock::time_point ts;
  if (perf.enabled)
    ts = std::chrono::steady_clock::now();
  auto r = node->getCopy();
  if (perf.enabled)
    perf.tSubCopy +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();

  if (perf.enabled)
    ts = std::chrono::steady_clock::now();
  int n = node->getMaxVname("Y[", "]");
  (void)ts;
  int start = (n == -1) ? 0 : n + 1;

  for (size_t i = 0; i < nodes.size(); ++i) {
    std::string vname = getVname(start + static_cast<int>(i));
    bool found = r->substituteAllOccurences(nodes[i], vname, onlyFullMatch);
    if (!found)
      return nullptr;
  }

  if (perf.enabled)
    perf.tSubMech +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();

  if (perf.enabled)
    ts = std::chrono::steady_clock::now();
  r->refine();
  r->markLinear();
  if (perf.enabled)
    perf.tSubRefine +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();

  if (perf.enabled)
    ts = std::chrono::steady_clock::now();
  collectAndEnumerateVariables(r);
  if (perf.enabled)
    perf.tSubTail +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();

  if (perf.enabled)
    ts = std::chrono::steady_clock::now();
  simplifySubexpression(r, nullptr, true, true);
  if (perf.enabled)
    perf.tSubSimplify +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();

  if (perf.enabled)
    ts = std::chrono::steady_clock::now();
  for (size_t i = 0; i < nodes.size(); ++i) {
    std::string vname = getVname(start + static_cast<int>(i));
    r->replaceVariable(vname, nodes[i]);
  }
  if (perf.enabled)
    perf.tSubMech +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();

  if (perf.enabled)
    ts = std::chrono::steady_clock::now();
  r->refine();
  if (perf.enabled)
    perf.tSubTail +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();
  return r;
}

// Returns true iff the second expression is more or equally complex than the
// first one.
bool GeneralSimplifier::isSecondMoreOrEquallyComplex(const std::shared_ptr<Node> &first,
                                                    const std::shared_ptr<Node> &second) const {
  int c1 = first->computeAlternation(nullptr);
  int c2 = second->computeAlternation(nullptr);

  if (c1 != c2)
    return c1 < c2;
  return computeBitwiseComplexity(*first) <= computeBitwiseComplexity(*second);
}

// Simplify the given node via substitution of the given nodes.
bool GeneralSimplifier::simplifyViaSubstitutionOfNodes(const std::shared_ptr<Node> &node,
                                                      const std::vector<std::shared_ptr<Node>> &nodes,
                                                      bool onlyFullMatch) {
  std::chrono::steady_clock::time_point ts;
  auto r = getSimplViaSubstitutionOfNodes(node, nodes, onlyFullMatch);

  if (r == nullptr)
    return false;
  if (perf.enabled)
    perf.substFound++;
  if (perf.enabled)
    ts = std::chrono::steady_clock::now();
  bool complex = isSecondMoreOrEquallyComplex(r, node);
  if (perf.enabled)
    perf.tComplexity +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();
  if (!complex)
    return false;

  if (perf.enabled) {
    long pc = static_cast<long>(nodes.size());
    if (pc - 1 < 4)
      perf.subsetImprovedByPop[pc - 1]++;
    perf.substImproved++;
  }
  if (perf.enabled)
    ts = std::chrono::steady_clock::now();
  node->copy(*r);

  if (node->refineAfterSubstitution()) {
    if (perf.enabled) {
      perf.substWalkChanged++;
      perf.tSubAccRefine +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();
      ts = std::chrono::steady_clock::now();
    }
    node->refine();
    if (perf.enabled)
      perf.tSubAccMarkLin +=
          std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();
    ts = std::chrono::steady_clock::now();
  }

  node->markLinear();
  if (perf.enabled) {
    perf.tSubAccMarkLin +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();
    perf.tSubAccept +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - ts).count();
  }

  return true;
}

// Simplify the given node via substitution of the selected nodes.
bool GeneralSimplifier::simplifyViaSubstitutionForIndex(const std::shared_ptr<Node> &node,
                                                       const std::vector<std::shared_ptr<Node>> &nodes,
                                                       int index) {
  int n = index;
  std::vector<std::shared_ptr<Node>> sel;
  for (size_t j = 0; j < nodes.size(); ++j) {
    if ((n & 1) == 1)
      sel.push_back(nodes[j]);
    n >>= 1;
  }

  if (perf.enabled) {
    perf.subsetTried++;
    long pc = static_cast<long>(sel.size());
    if (pc - 1 < 4)
      perf.subsetTriedByPop[pc - 1]++;
  }
  bool changed = false;
  if (simplifyViaSubstitutionOfNodes(node, sel, false))
    changed = true;
  // Skip the full-match pass when the non-full-match pass already succeeded
  // and mutated the node: the full-match pass is then largely redundant and
  // costs a second deep-copy + recursive re-simplification per subset. Measured
  // on the nonpoly dataset this saves ~36% of the substitution time. The tool
  // still verifies every simplification against the original, so a missed
  // (but valid) full-match simplification only means "less simplified", never
  // a wrong result.
  else if (simplifyViaSubstitutionOfNodes(node, sel, true))
    changed = true;

  return changed;
}

// Simplify the given node via substitution.
bool GeneralSimplifier::simplifyViaSubstitution(const std::shared_ptr<Node> &node) {
  // Phase-1: enforce the per-simplify() substitution budget. Whatever was
  // applied before the budget was exhausted is a verified-equivalent rewrite;
  // stopping early only means "less simplified", never a wrong result.
  if ((substBudgetMs > 0 && substTimeSpentMs >= substBudgetMs) ||
      (substBudgetSubsets > 0 && substSubsetsSpent >= substBudgetSubsets))
    return false;

  std::vector<std::shared_ptr<Node>> nodes = collectNodesForSubstitution(node);
  if (nodes.empty())
    return false;

  if (perf.enabled) {
    perf.substCalls++;
    size_t n = nodes.size();
    perf.nodesHist[n <= 4 ? 0 : (n <= 9 ? 1 : (n <= 20 ? 2 : 3))]++;
  }

  bool changed = false;

  auto tCall = std::chrono::steady_clock::now();

  // The Python source iterates i in [1, 2**len(nodes)); cap the range at 2^20
  // (subsets involving higher indices are not tried; the popcount filters
  // below mirror the source).
  int nBits = std::min(static_cast<int>(nodes.size()), 20);
  for (int i = 1; i < (1 << nBits); ++i) {
    if (std::chrono::steady_clock::now() > deadline)
      break;

    // Phase-1: stop this call once the global substitution budget is spent,
    // so a single node cannot blow far past it.
    if (substBudgetMs > 0 &&
        substTimeSpentMs +
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - tCall)
                    .count() >=
            substBudgetMs)
      break;
    if (substBudgetSubsets > 0 && substSubsetsSpent >= substBudgetSubsets)
      break;

    if (nodes.size() > 5 && MBAOps::popcount(static_cast<uint64_t>(i)) > 3)
      continue;
    if (nodes.size() > 9 && MBAOps::popcount(static_cast<uint64_t>(i)) > 2)
      continue;

    ++substSubsetsSpent;
    if (simplifyViaSubstitutionForIndex(node, nodes, i))
      changed = true;
  }

  substTimeSpentMs +=
      std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - tCall)
          .count();

  return changed;
}

// Simplify the given subexpression.
bool GeneralSimplifier::simplifySubexpression(const std::shared_ptr<Node> &node,
                                             const std::shared_ptr<Node> &parent, bool noRefactor,
                                             bool noSubst) {
  // Tier 2 first-class operator nodes (>> / / %) are opaque leaves: simplify
  // their children but never refine/rewrite the operator node itself.
  if (node->type == NodeType::RSHIFT || node->type == NodeType::UDIV ||
      node->type == NodeType::UREM) {
    bool changed = false;
    for (auto &c : node->children)
      if (simplifySubexpression(c, node, noRefactor, noSubst))
        changed = true;
    return changed;
  }

  if (node->isLinear()) {
    bool ch = simplifyLinearSubexpression(node);
    return ch;
  }

  bool changed = false;
  for (auto &c : node->children) {
    bool ch = simplifySubexpression(c, node, noRefactor, noSubst);
    if (ch)
      changed = true;
  }

  std::string before = node->toString();
  node->refine(parent.get(), true);
  node->markLinear(true);

  if (!changed)
    changed = node->toString() != before;

  if (node->type == NodeType::CONSTANT) {
    return changed;
  }

  if (node->isLinear()) {
    simplifyLinearSubexpression(node);
    return true;
  }

  if (simplifyNonlinearSubexpression(node, parent, noRefactor, noSubst))
    changed = true;

  return changed;
}

// Verify the simplification using Z3: proves orig == simpl with the
// project's Z3 backend (bitCount-bit modular semantics, see Verify.cpp).
// Returns false (and thus rejects the result) when the proof does not go
// through — never trust an unverified result.
bool GeneralSimplifier::verifyUsingZ3(const std::string &orig, const std::string &simpl) {
  return proveEquivalent(orig, simpl, bitCount);
}

// Verify the given original expression against the simplified tree by
// exhaustive evaluation.
bool GeneralSimplifier::checkVerify(const std::string &orig,
                                    const std::shared_ptr<Node> &simplTree) {
  if (verifBitCount == -1)
    return true;

  auto origTree = parse(orig, bitCount, modRed, false, false);
  if (origTree == nullptr)
    return false;

  return simplTree->checkVerify(origTree, verifBitCount);
}

// Phase 2: direct single-pass (CoBRA-style) simplification.
std::string GeneralSimplifier::simplifyDirect(const std::string &expr) {
  auto root = parse(expr, bitCount, modRed, true, true);
  if (root == nullptr)
    return "";
  // Phase 2: the direct transform only wins for high-varCount non-polynomial
  // MBAs, where the iterative substitution search thrashes over 2^vars subsets
  // for a small net gain (e.g. the 9-16 var obfuscatorx cases: direct gives a
  // verified result in ~2ms vs ~30s for the full search). For low-varCount or
  // polynomial MBAs the iterative path is both faster and yields a more
  // compact result (measured: direct is up to ~5x larger on 2-var
  // permutation64), so defer to it there.
  std::vector<std::string> dvars;
  root->collectVariables(dvars);
  if (static_cast<int>(dvars.size()) < 6)
    return "";
  // One bottom-up linear (AND-basis) decomposition pass. noRefactor + noSubst
  // means the per-node fixed-point loop runs at most ~1-2 iterations (the
  // linear part, then the isLinear check) — a direct transform, not a search.
  simplifySubexpression(root, nullptr, /*noRefactor=*/true, /*noSubst=*/true);
  root->polish();
  std::string simpl = root->toString();
  if (simpl == expr)
    return ""; // no change: not a useful direct result
  // Full-width verification gate (the direct path skips the per-node checks
  // the iterative path performs). Never return an unverified result.
  if (!fastCheckEquivalent(expr, simpl, bitCount, 100, true))
    return "";
  return simpl;
}

// Simplify the given expression.
std::string GeneralSimplifier::simplify(const std::string &expr, bool useZ3) {
  noChangeFingerprint.clear();  // A1: fresh per expression
  if (perf.enabled) {
    linearSeen.clear();  // B-i: fresh per expression
    for (int i = 0; i < 13; ++i) {
      checkPerf().t[i] = 0;
      checkPerf().calls[i] = 0;
      checkPerf().fired[i] = 0;
    }
    checkPerf().mlSkips = 0;
    checkPerf().mlRecomputes = 0;
  }
  linearSimplifyMemo.clear();  // W1: fresh per expression (bounded memory)
  substTimeSpentMs = 0;         // Phase-1: fresh substitution budget per call
  substSubsetsSpent = 0;

  std::chrono::steady_clock::time_point tStart;
  if (perf.enabled)
    tStart = std::chrono::steady_clock::now();

  // Mirror the Python 30s timeout with a wall-clock deadline.
  deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSec);

  // Phase 2: try the direct single-pass first (CoBRA-style fast verified
  // equivalent). If it yields a result, return it immediately — no iterative
  // refactor/substitution search. On "", fall through to the full path.
  if (directEnabled) {
    std::string direct = simplifyDirect(expr);
    if (!direct.empty())
      return direct;
  }

  std::string result;
  auto root = parse(expr, bitCount, modRed, true, true);
  if (root != nullptr) {
    simplifySubexpression(root, nullptr);

    root->polish();

    std::string simpl = root->toString();

    bool z3ok = !useZ3 || verifyUsingZ3(expr, simpl);
    if (z3ok && checkVerify(expr, root))
      result = simpl;
  }

  if (perf.enabled) {
    perf.tTotal +=
        std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
    fprintf(stderr,
            "PERF iters=%ld refactor=%ld skips=%ld tLinear=%.4f tRefactor=%.4f "
            "tSubst=%.4f tTotal=%.4f "
            "substCalls=%ld subsetTried=%ld substFound=%ld substImproved=%ld "
            "substWalkChanged=%ld "
            "triedByPop=[%ld,%ld,%ld,%ld] impByPop=[%ld,%ld,%ld,%ld] "
            "nodesHist=[%ld,%ld,%ld,%ld] "
            "loopNoChange=%ld loopCycle=%ld loopDeadline=%ld loopMaxIt=%ld "
            "tSubCopy=%.4f tSubRefine=%.4f tSubSimplify=%.4f "
            "tSubMech=%.4f tSubTail=%.4f tSubAccept=%.4f tSubAccRefine=%.4f "
            "tSubAccMarkLin=%.4f tComplexity=%.4f "
            "tLinearSub=%.4f tLinearMba=%.4f tLinearParse=%.4f "
            "linearSubCalls=%ld linearSubChanged=%ld linearSubDupCalls=%ld "
            "linearCacheHits=%ld toStringCalls=%ld toStringT=%.4f "
            "checkT=%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f "
            "checkFired=%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld\n",
            perf.iters, perf.refactorCalls, perf.refactorSkips, perf.tLinear,
            perf.tRefactor, perf.tSubst, perf.tTotal, perf.substCalls,
            perf.subsetTried, perf.substFound, perf.substImproved,
            perf.substWalkChanged,
            perf.subsetTriedByPop[0], perf.subsetTriedByPop[1], perf.subsetTriedByPop[2],
            perf.subsetTriedByPop[3], perf.subsetImprovedByPop[0],
            perf.subsetImprovedByPop[1], perf.subsetImprovedByPop[2],
            perf.subsetImprovedByPop[3], perf.nodesHist[0], perf.nodesHist[1],
            perf.nodesHist[2], perf.nodesHist[3], perf.loopNoChange, perf.loopCycle,
            perf.loopDeadline, perf.loopMaxIt, perf.tSubCopy, perf.tSubRefine,
            perf.tSubSimplify, perf.tSubMech, perf.tSubTail, perf.tSubAccept,
            perf.tSubAccRefine, perf.tSubAccMarkLin, perf.tComplexity,
            perf.tLinearSub,
            perf.tLinearMba, perf.tLinearParse, perf.linearSubCalls,
            perf.linearSubChanged, perf.linearSubDupCalls, perf.linearCacheHits,
            toStringPerf().calls.load(),
            toStringPerf().nanos.load() / 1e9,
            checkPerf().t[0], checkPerf().t[1], checkPerf().t[2], checkPerf().t[3],
            checkPerf().t[4], checkPerf().t[5], checkPerf().t[6], checkPerf().t[7],
            checkPerf().t[8], checkPerf().t[9], checkPerf().t[10], checkPerf().t[11],
            checkPerf().t[12], checkPerf().fired[0], checkPerf().fired[1],
            checkPerf().fired[2], checkPerf().fired[3], checkPerf().fired[4],
            checkPerf().fired[5], checkPerf().fired[6], checkPerf().fired[7],
            checkPerf().fired[8], checkPerf().fired[9], checkPerf().fired[10],
            checkPerf().fired[11], checkPerf().fired[12]);
    fprintf(stderr, "PERF_ML skips=%ld recomputes=%ld\n", checkPerf().mlSkips,
            checkPerf().mlRecomputes);
  }

  return result;
}

// Simplify the given expression with the given number of bits.
std::string simplifyMba(const std::string &expr, int bitCount, bool useZ3, bool modRed,
                        int verifBitCount, int timeoutSec) {
  GeneralSimplifier simplifier(bitCount, modRed, verifBitCount, timeoutSec);
  return simplifier.simplify(expr, useZ3);
}

} // namespace MBA
} // namespace LSiMBA
