// GAMBA native C++ port — Node implementation.
#include "Node.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <set>
#include <sstream>

namespace LSiMBA {
namespace MBA {

using namespace std;

// Global toString accounting (MBASIMBA_PERF=1), read by GeneralSimplifier's
// PERF line. Thread-safe via atomics; cheap (one flag load) when off.
// W3-debug: validate the markLinear fast path against the full recompute.
bool &mlCheckModeRef() {
  static bool v = [] {
    const char *p = std::getenv("MBASIMBA_MLCHECK");
    return (p != nullptr && p[0] == '1');
  }();
  return v;
}
bool mlCheckMode = mlCheckModeRef();

// W3: the markLinear fast path. On this dataset its skip rate (72%) does not
// beat its per-call hash overhead, so it is OFF by default; MBASIMBA_MLFast=1
// re-enables it. The self-check (MBASIMBA_MLCHECK=1) validates it either way.
bool &mlFastEnabledRef() {
  static bool v = [] {
    const char *p = std::getenv("MBASIMBA_MLFast");
    return (p != nullptr && p[0] == '1');
  }();
  return v;
}
bool mlFastEnabled = mlFastEnabledRef();

// W3-debug: assert that a node's state equals a full recompute of its
// current context (states only; linearEnd is a derived cache that fresh
// copies legitimately leave at 0). Aborts at the corruption origin.
void mlAssertConsistent(const Node &n, const char *where) {
  static bool inAssert = false;
  if (inAssert)
    return;
  inAssert = true;
  auto shadow = n.getCopy();
  inAssert = false;
  shadow->invalidatePatternCaches();
  shadow->markLinearFull();
  auto seq = [](const Node &root) {
    std::string s;
    std::function<void(const Node &)> rec = [&](const Node &m) {
      s += "[" + std::to_string((int)m.type) + ":" + std::to_string((int)m.state) +
          ":" + m.vname + ":" + std::to_string((int)m.constant.getSExtValue()) + ":";
      for (auto &cc : m.children)
        rec(*cc);
      s += "]";
    };
    rec(root);
    return s;
  };
  if (seq(n) != seq(*shadow)) {
    fprintf(stderr, "MLCHECK INCONSISTENT SOURCE in %s:\n mine=%s\n full=%s\n",
            where, seq(n).c_str(), seq(*shadow).c_str());
    std::abort();
  }
}

CheckPerf &checkPerf() {
  static CheckPerf inst;
  static bool init = [] {
    const char *p = std::getenv("MBASIMBA_PERF");
    if (p && p[0] == '1')
      inst.enabled = true;
    return true;
  }();
  (void)init;
  return inst;
}

ToStringPerf &toStringPerf() {
  static ToStringPerf inst;
  static bool init = [] {
    const char *p = std::getenv("MBASIMBA_PERF");
    if (p != nullptr && p[0] == '1')
      inst.enabled.store(true);
    return true;
  }();
  (void)init;
  return inst;
}

// Mirrors node.py Node.to_string (top-level-timed wrapper around
// toStringImpl; nested recursive calls are not double-counted).
string Node::toString(bool withParentheses, int end,
                      const vector<string> *varNames) {
  thread_local bool inToString = false;
  auto &impl = toStringPerf();
  if (!impl.enabled.load() || inToString)
    return toStringImpl(withParentheses, end, varNames);
  inToString = true;
  impl.calls++;
  auto t0 = std::chrono::steady_clock::now();
  string r = toStringImpl(withParentheses, end, varNames);
  impl.nanos += std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - t0)
                    .count();
  inToString = false;
  return r;
}

string Node::toStringImpl(bool withParentheses, int end,
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
    string ret = child1->toStringImpl(typeRank(child1->type) > typeRank(NodeType::VARIABLE), -1,
                                  varNames) +
                 "**" +
                 child2->toStringImpl(typeRank(child2->type) > typeRank(NodeType::VARIABLE), -1,
                                  varNames);
    if (withParentheses)
      ret = "(" + ret + ")";
    return ret;
  }

  if (type == NodeType::NEGATION) {
    auto child = children[0];
    string ret = "~" +
                child->toStringImpl(typeRank(child->type) > typeRank(NodeType::NEGATION), -1,
                                varNames);
    if (withParentheses)
      ret = "(" + ret + ")";
    return ret;
  }

  if (type == NodeType::PRODUCT) {
    auto child1 = children[0];
    string ret1 =
        child1->toStringImpl(typeRank(child1->type) > typeRank(NodeType::PRODUCT), -1, varNames);
    string ret = ret1;
    for (int i = 1; i < end; ++i) {
      ret += "*" +
             children[i]->toStringImpl(
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
    string ret = child1->toStringImpl(typeRank(child1->type) > typeRank(NodeType::SUM), -1, varNames);
    for (int i = 1; i < end; ++i) {
      string s =
          children[i]->toStringImpl(typeRank(children[i]->type) > typeRank(NodeType::SUM), -1,
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
        child1->toStringImpl(typeRank(child1->type) > typeRank(NodeType::CONJUNCTION), -1, varNames);
    for (int i = 1; i < end; ++i) {
      ret += "&" +
             children[i]->toStringImpl(
                 typeRank(children[i]->type) > typeRank(NodeType::CONJUNCTION), -1, varNames);
    }
    if (withParentheses)
      ret = "(" + ret + ")";
    return ret;
  }

  if (type == NodeType::EXCL_DISJUNCTION) {
    auto child1 = children[0];
    string ret = child1->toStringImpl(
        typeRank(child1->type) > typeRank(NodeType::EXCL_DISJUNCTION), -1, varNames);
    for (int i = 1; i < end; ++i) {
      ret += "^" +
             children[i]->toStringImpl(
                 typeRank(children[i]->type) > typeRank(NodeType::EXCL_DISJUNCTION), -1, varNames);
    }
    if (withParentheses)
      ret = "(" + ret + ")";
    return ret;
  }

  if (type == NodeType::INCL_DISJUNCTION) {
    auto child1 = children[0];
    string ret = child1->toStringImpl(
        typeRank(child1->type) > typeRank(NodeType::INCL_DISJUNCTION), -1, varNames);
    for (int i = 1; i < end; ++i) {
      ret += "|" +
             children[i]->toStringImpl(
                 typeRank(children[i]->type) > typeRank(NodeType::INCL_DISJUNCTION), -1, varNames);
    }
    if (withParentheses)
      ret = "(" + ret + ")";
    return ret;
  }

  // Tier 2 first-class operator nodes (exact unsigned semantics).
  if (type == NodeType::RSHIFT || type == NodeType::UDIV || type == NodeType::UREM) {
    const char *op = (type == NodeType::RSHIFT) ? ">>" : (type == NodeType::UDIV ? "/" : "%");
    auto child1 = children[0];
    string ret =
        child1->toStringImpl(ChildNeedsParens(child1->type, type), -1, varNames) +
        op +
        children[1]->toStringImpl(ChildNeedsParens(children[1]->type, type), -1, varNames);
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
  // Tier 2 first-class operator nodes (exact unsigned semantics, mod 2^B).
  if (type == NodeType::RSHIFT)
    return y >= static_cast<std::uint64_t>(bitCount) ? 0 : x >> y;
  if (type == NodeType::UDIV)
    return y == 0 ? 0 : x / y;
  if (type == NodeType::UREM)
    return y == 0 ? 0 : x % y;
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
  return type == NodeType::SUM || type == NodeType::PRODUCT || type == NodeType::POWER ||
         type == NodeType::RSHIFT || type == NodeType::UDIV || type == NodeType::UREM;
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
  if (!restrictedScope) {
    if (mlFastEnabled)
      markLinearFast();
    else
      markLinearFull();
    return;
  }

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
  else if (type == NodeType::RSHIFT || type == NodeType::UDIV || type == NodeType::UREM)
    state = NodeState::NONLINEAR; // opaque leaf for the general simplifier
  else if (type == NodeType::VARIABLE)
    markLinearVariable();
  else if (type == NodeType::CONSTANT)
    markLinearConstant();

  reorderAndDetermineLinearEnd();
}

void Node::markLinearFull() {
  for (auto &c : children)
    c->markLinearFull();
  if (type == NodeType::INCL_DISJUNCTION || type == NodeType::EXCL_DISJUNCTION ||
      type == NodeType::CONJUNCTION || type == NodeType::NEGATION)
    markLinearBitwise();
  else if (type == NodeType::SUM)
    markLinearSum();
  else if (type == NodeType::PRODUCT)
    markLinearProduct();
  else if (type == NodeType::POWER)
    markLinearPower();
  else if (type == NodeType::RSHIFT || type == NodeType::UDIV || type == NodeType::UREM)
    state = NodeState::NONLINEAR;
  else if (type == NodeType::VARIABLE)
    markLinearVariable();
  else if (type == NodeType::CONSTANT)
    markLinearConstant();
  reorderAndDetermineLinearEnd();
}

void Node::invalidatePatternCaches() {
  mlKeyValid = false;
  patternHashValid = false;
  for (auto &c : children)
    c->invalidatePatternCaches();
}

namespace {
inline uint64_t mlFnv1a64(uint64_t h, uint64_t x) {
  h ^= x;
  h *= 1099511628211ULL;
  return h;
}
} // namespace

bool Node::markLinearFast() {
  bool allStable = true;
  for (auto &c : children)
    if (!c->markLinearFast())
      allStable = false;

  // Structural fingerprint (no object pointers): preserved by deep copies,
  // so getCopy propagates the cached fingerprint. The constant IS included:
  // markLinearConstant's result depends on it (0 / -1 are BITWISE).
  // Captured PRE-reorder (the cache stores the pre-reorder form; after a
  // moving reorder the hash misses and the next call settles the cache).
  uint64_t h1 = 1469598103934665603ULL;
  uint64_t h2 = 1469598103934665604ULL;
  auto mlFold = [&h1, &h2](uint64_t x) {
    h1 = mlFnv1a64(h1, x);
    h2 = mlFnv1a64(h2, x ^ 0x9E3779B97F4A7C15ULL);
  };
  mlFold(static_cast<uint64_t>(type));
  mlFold(constant.getZExtValue());
  mlFold(constant.shl(64).getZExtValue());
  for (auto &c : children) {
    mlFold(static_cast<uint64_t>(c->type));
    mlFold(static_cast<uint64_t>(c->state));
  }
  // MBASIMBA_MLCHECK: exact structural context for collision-free key
  // validation (debug only; the hash path above is the production key).
  std::vector<uint64_t> ctxVec;
  if (mlCheckMode) {
    ctxVec.reserve(3 + 2 * children.size());
    ctxVec.push_back(static_cast<uint64_t>(type));
    ctxVec.push_back(constant.getZExtValue());
    ctxVec.push_back(constant.shl(64).getZExtValue());
    for (auto &c : children) {
      ctxVec.push_back(static_cast<uint64_t>(c->type));
      ctxVec.push_back(static_cast<uint64_t>(c->state));
    }
  }

  static std::set<int> noSkipTypes = [] {
    std::set<int> s;
    if (const char *p = std::getenv("MBASIMBA_MLSKIP_OFF")) {
      std::stringstream ss(p);
      std::string tok;
      while (std::getline(ss, tok, ','))
        if (!tok.empty())
          s.insert(std::stoi(tok));
    }
    return s;
  }();
  bool exactCtxMatch = !mlCheckMode || (ctxVec == mlKeyCtx);
  if (mlCheckMode && h1 == mlKey1 && h2 == mlKey2 && !exactCtxMatch) {
    fprintf(stderr, "MLCHECK HASH COLLISION: type=%d tree=%s\n", (int)type,
            toString().c_str());
    std::abort();
  }
  const bool noSkip =
      !noSkipTypes.empty() && noSkipTypes.count(static_cast<int>(type));
  if (allStable && mlKeyValid && h1 == mlKey1 && h2 == mlKey2 && exactCtxMatch &&
      !noSkip) {
    if (checkPerf().enabled)
      checkPerf().mlSkips++;
    // linearEnd is a derived cache; a matching key means (state, context) is
    // unchanged since it was last derived, so it is still consistent — except
    // when this node came from getCopy/getShallowCopy/copyAll, which leave it
    // at 0. Refresh only in that case (idempotent O(n) count + re-order).
    if (linearEnd == 0)
      reorderAndDetermineLinearEnd();
    if (mlCheckMode) {
      auto shadow = getCopy();
      shadow->invalidatePatternCaches();
      shadow->markLinearFull();
      auto seq = [](const Node &n) {
        std::string s;
        std::function<void(const Node &)> rec = [&](const Node &m) {
          s += "[" + std::to_string((int)m.type) + ":" + std::to_string((int)m.state) +
              ":" + std::to_string(m.linearEnd) + ":" + m.vname +
              ":" + std::to_string((int)m.constant.getSExtValue()) + ":";
          for (auto &cc : m.children)
            rec(*cc);
          s += "]";
        };
        rec(n);
        return s;
      };
      if (seq(*this) != seq(*shadow)) {
        // Decisive: what does the pure dispatch give for the current ctx?
        NodeState fstate;
        if (type == NodeType::INCL_DISJUNCTION || type == NodeType::EXCL_DISJUNCTION ||
            type == NodeType::CONJUNCTION || type == NodeType::NEGATION)
          fstate = children.empty() ? NodeState::BITWISE
                                     : (std::all_of(children.begin(), children.end(),
                                                     [](const auto &cc) {
                                                       return cc->state == NodeState::BITWISE;
                                                     })
                                           ? NodeState::BITWISE
                                           : NodeState::MIXED);
        else if (type == NodeType::SUM)
          fstate = [&] {
            NodeState st = NodeState::UNKNOWN;
            for (auto &cc : children) {
              if (cc->state == NodeState::MIXED)
                return NodeState::MIXED;
              if (cc->state == NodeState::NONLINEAR)
                st = NodeState::NONLINEAR;
            }
            return st == NodeState::NONLINEAR ? st : NodeState::LINEAR;
          }();
        else
          fstate = NodeState::UNKNOWN; // not critical
        fprintf(stderr,
                "MLCHECK MISMATCH at type=%d state=%d le=%d f(ctx)=%d shadow=%d "
                "keyValid=%d ctxMatch=%d\n",
                (int)type, (int)state, linearEnd, (int)fstate, (int)shadow->state,
                (int)mlKeyValid, (int)exactCtxMatch);
        for (size_t i = 0; i < children.size(); ++i) {
          auto live = children[i];
          auto sh = shadow->children[i];
          if ((int)live->state != (int)sh->state || live->linearEnd != sh->linearEnd) {
            fprintf(stderr,
                    "  STALE CHILD %zu: live(state=%d le=%d) full(state=%d le=%d) "
                    "childTree=%s\n", i, (int)live->state, live->linearEnd, (int)sh->state,
                    sh->linearEnd, live->toString().c_str());
          }
        }
        fprintf(stderr, "  mine=%s\n  full=%s\n", seq(*this).c_str(), seq(*shadow).c_str());
        std::abort();
      }
    }
    return true; // recompute would set the identical state; skip
  }

  auto oldState = state;
  const bool keyMatched = mlKeyValid && h1 == mlKey1 && h2 == mlKey2;

  if (type == NodeType::INCL_DISJUNCTION || type == NodeType::EXCL_DISJUNCTION ||
      type == NodeType::CONJUNCTION || type == NodeType::NEGATION)
    markLinearBitwise();
  else if (type == NodeType::SUM)
    markLinearSum();
  else if (type == NodeType::PRODUCT)
    markLinearProduct();
  else if (type == NodeType::POWER)
    markLinearPower();
  else if (type == NodeType::RSHIFT || type == NodeType::UDIV || type == NodeType::UREM)
    state = NodeState::NONLINEAR;
  else if (type == NodeType::VARIABLE)
    markLinearVariable();
  else if (type == NodeType::CONSTANT)
    markLinearConstant();

  reorderAndDetermineLinearEnd();

  mlKey1 = h1;
  mlKey2 = h2;
  mlKeyValid = true;
  if (mlCheckMode) {
    mlKeyCtx = std::move(ctxVec);
    auto shadow = getCopy();
    shadow->invalidatePatternCaches();
    shadow->markLinearFull();
    if ((int)shadow->state != (int)state || shadow->linearEnd != linearEnd) {
      fprintf(stderr,
              "MLCHECK BAD CACHE on recompute: type=%d oldState=%d newState=%d "
              "fullState=%d le(mine=%d full=%d) allStable=%d keyWasValid=%d "
              "keyMatched=%d tree=%s\n",
              (int)type, (int)oldState, (int)state, (int)shadow->state, linearEnd,
              shadow->linearEnd, (int)allStable, (int)mlKeyValid, (int)keyMatched,
              toString().c_str());
      std::abort();
    }
  }
  if (checkPerf().enabled)
    checkPerf().mlRecomputes++;
  return state == oldState;
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
        // Python uses list.remove(child), which removes the first element that
        // IS `child` (identity; Node has no __eq__). A children vector may hold
        // the same object twice (aliasing via copy), so erasing by index i would
        // remove a different occurrence than Python does.
        auto it = std::find_if(children.begin(), children.end(),
                               [&child](const std::shared_ptr<Node> &p) {
                                 return p.get() == child.get();
                               });
        children.erase(it);
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
        // Match Python's list.remove(child) (remove by identity, first
        // occurrence) rather than erasing by index; see the non-product
        // variant above for why this matters under aliasing.
        auto it = std::find_if(children.begin(), children.end(),
                               [&child](const std::shared_ptr<Node> &p) {
                                 return p.get() == child.get();
                               });
        children.erase(it);
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
  // The Tier 2 first-class operator nodes (>> / / %) are not commutative:
  // never reorder their children (a / b != b / a).
  if (type == NodeType::RSHIFT || type == NodeType::UDIV || type == NodeType::UREM)
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
  bool mv = node.mlKeyValid;
  uint64_t mk1 = node.mlKey1;
  uint64_t mk2 = node.mlKey2;
  std::vector<uint64_t> mkc = node.mlKeyCtx;
  int le = node.linearEnd;

  type = t;
  state = s;
  children = ch; // alias (shared children), like Python
  vname = vn;
  vidx = vi;
  constant = c;
  // The transplanted (state, key) pair must come from the same node and the
  // same instant: keeping this node's own (older) key next to `node`'s state
  // is unsound — if the aliased children are structurally identical to this
  // node's former children, the stale key matches and the fast path skips
  // forever, keeping the transplanted state even as the shared children are
  // re-marked. Propagating `node`'s key is safe: this node's current context
  // is exactly `node`'s (same children, constant, type), so a key match
  // still implies the state is up to date, and any later re-mark of a shared
  // child changes the context and defeats the key.
  mlKeyValid = mv;
  mlKey1 = mk1;
  mlKey2 = mk2;
  mlKeyCtx = std::move(mkc);
  linearEnd = le; // sound by the same argument as the key above
}

void Node::copyAll(const Node &node) {
  type = node.type;
  state = node.state;
  children.clear();
  vname = node.vname;
  vidx = node.vidx;
  constant = node.constant;
  mlKeyValid = node.mlKeyValid;
  mlKey1 = node.mlKey1;
  mlKey2 = node.mlKey2;
  mlKeyCtx = node.mlKeyCtx;
  patternHashValid = node.patternHashValid;
  patternHash1 = node.patternHash1;
  patternHash2 = node.patternHash2;
  for (auto &child : node.children)
    children.push_back(child->getCopy());
}

shared_ptr<Node> Node::getCopy() const {
  auto n = newNode(type);
  n->state = state;
  n->vname = vname;
  n->vidx = vidx;
  n->constant = constant;
  n->mlKeyValid = mlKeyValid;
  n->mlKey1 = mlKey1;
  n->mlKey2 = mlKey2;
  n->mlKeyCtx = mlKeyCtx;
  n->patternHashValid = patternHashValid;
  n->patternHash1 = patternHash1;
  n->patternHash2 = patternHash2;
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
  n->mlKeyValid = mlKeyValid;
  n->mlKey1 = mlKey1;
  n->mlKey2 = mlKey2;
  n->mlKeyCtx = mlKeyCtx;
  n->patternHashValid = patternHashValid;
  n->patternHash1 = patternHash1;
  n->patternHash2 = patternHash2;
  return n;
}

} // namespace MBA
} // namespace LSiMBA
