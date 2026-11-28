// simba/Core.h implementation: expression trees over LSiMBA::MBA::Node and
// simplify() on top of the router and the native linear simplifier.
#include "simba/Core.h"

#include "MBA/Node.h"
#include "MBA/Parser.h"
#include "Simplifier.h"
#include "SimplifierRouter.h"

#include "MBA/Verify.h"
#include <z3++.h>
#include <algorithm>
#include <map>
#include <vector>
#include "MBAChecker.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <cstdio>
#ifdef _WIN32
#include <io.h>
#define SIMBA_DUP _dup
#define SIMBA_DUP2 _dup2
#define SIMBA_CLOSE _close
#define SIMBA_OPEN_NULL() _open("NUL", _O_WRONLY)
#include <fcntl.h>
#else
#include <fcntl.h>
#include <unistd.h>
#define SIMBA_DUP dup
#define SIMBA_DUP2 dup2
#define SIMBA_CLOSE close
#define SIMBA_OPEN_NULL() open("/dev/null", O_WRONLY)
#endif

extern llvm::cl::opt<std::string> SimplifierChoice; // SimplifierRouter.cpp

using LSiMBA::MBA::Node;
using LSiMBA::MBA::NodeType;

namespace simba {

//===--- Expr --------------------------------------------------------------===//

Kind Expr::kind() const {
  switch (node->type) {
  case NodeType::CONSTANT: return Kind::Constant;
  case NodeType::VARIABLE: return Kind::Variable;
  case NodeType::POWER: return Kind::Pow;
  case NodeType::NEGATION: return Kind::Not;
  case NodeType::PRODUCT: return Kind::Mul;
  case NodeType::SUM: return Kind::Add;
  case NodeType::CONJUNCTION: return Kind::And;
  case NodeType::EXCL_DISJUNCTION: return Kind::Xor;
  case NodeType::INCL_DISJUNCTION: return Kind::Or;
  case NodeType::RSHIFT: return Kind::LShr;
  case NodeType::UDIV: return Kind::UDiv;
  case NodeType::UREM: return Kind::URem;
  }
  return Kind::Constant;
}

unsigned Expr::numOperands() const { return node->children.size(); }

Expr Expr::operand(unsigned i) const { return fromRaw(node->children[i]); }

llvm::APInt Expr::constant() const {
  return node->constant.zextOrTrunc(node->bitCount);
}

unsigned Expr::variable() const {
  if (node->vname.size() > 1 && node->vname[0] == 'x')
    return std::stoul(node->vname.substr(1));
  return node->vidx < 0 ? 0 : unsigned(node->vidx);
}

unsigned Expr::bitWidth() const { return node->bitCount; }

std::string Expr::str() const { return node->toString(); }

//===--- ExprBuilder -------------------------------------------------------===//

ExprBuilder::ExprBuilder(unsigned bitWidth) : width(bitWidth) {}

Expr ExprBuilder::constant(const llvm::APInt &v) {
  auto n = std::make_shared<Node>(NodeType::CONSTANT, width);
  n->constant = v.zextOrTrunc(width).zext(128);
  n->reduceConstant();
  return Expr::fromRaw(n);
}

Expr ExprBuilder::constant(uint64_t v) {
  return constant(llvm::APInt(width, v, /*isSigned=*/false,
                              /*implicitTrunc=*/true));
}

Expr ExprBuilder::variable(unsigned index) {
  auto n = std::make_shared<Node>(NodeType::VARIABLE, width);
  n->vname = "x" + std::to_string(index);
  return Expr::fromRaw(n);
}

static NodeType nodeType(Kind k) {
  switch (k) {
  case Kind::Add: return NodeType::SUM;
  case Kind::Mul: return NodeType::PRODUCT;
  case Kind::Pow: return NodeType::POWER;
  case Kind::Not: return NodeType::NEGATION;
  case Kind::And: return NodeType::CONJUNCTION;
  case Kind::Or: return NodeType::INCL_DISJUNCTION;
  case Kind::Xor: return NodeType::EXCL_DISJUNCTION;
  case Kind::LShr: return NodeType::RSHIFT;
  case Kind::UDiv: return NodeType::UDIV;
  case Kind::URem: return NodeType::UREM;
  case Kind::Constant: return NodeType::CONSTANT;
  case Kind::Variable: return NodeType::VARIABLE;
  }
  return NodeType::CONSTANT;
}

Expr ExprBuilder::binary(Kind k, Expr a, Expr b) {
  auto n = std::make_shared<Node>(nodeType(k), width);
  n->children = {a.raw(), b.raw()};
  return Expr::fromRaw(n);
}

Expr ExprBuilder::add(Expr a, Expr b) { return binary(Kind::Add, a, b); }
Expr ExprBuilder::mul(Expr a, Expr b) { return binary(Kind::Mul, a, b); }
Expr ExprBuilder::neg(Expr a) {
  return mul(constant(llvm::APInt::getAllOnes(width)), a);
}
Expr ExprBuilder::sub(Expr a, Expr b) { return add(a, neg(b)); }
Expr ExprBuilder::pow(Expr a, uint64_t e) {
  return binary(Kind::Pow, a, constant(e));
}
Expr ExprBuilder::bitNot(Expr a) {
  auto n = std::make_shared<Node>(NodeType::NEGATION, width);
  n->children = {a.raw()};
  return Expr::fromRaw(n);
}
Expr ExprBuilder::bitAnd(Expr a, Expr b) { return binary(Kind::And, a, b); }
Expr ExprBuilder::bitOr(Expr a, Expr b) { return binary(Kind::Or, a, b); }
Expr ExprBuilder::bitXor(Expr a, Expr b) { return binary(Kind::Xor, a, b); }
Expr ExprBuilder::shl(Expr a, unsigned amount) {
  return mul(constant(llvm::APInt::getOneBitSet(width, amount % width)), a);
}
Expr ExprBuilder::lshr(Expr a, Expr b) { return binary(Kind::LShr, a, b); }
Expr ExprBuilder::udiv(Expr a, Expr b) { return binary(Kind::UDiv, a, b); }
Expr ExprBuilder::urem(Expr a, Expr b) { return binary(Kind::URem, a, b); }

Expr ExprBuilder::import(const Expr &e) {
  switch (e.kind()) {
  case Kind::Constant: {
    llvm::APInt c = e.constant();
    return constant(c.sextOrTrunc(width));
  }
  case Kind::Variable:
    return variable(e.variable());
  case Kind::Not:
    return bitNot(import(e.operand(0)));
  default:
    break;
  }
  auto n = std::make_shared<Node>(nodeType(e.kind()), width);
  for (unsigned i = 0; i < e.numOperands(); ++i)
    n->children.push_back(import(e.operand(i)).raw());
  return Expr::fromRaw(n);
}

//===--- simplify ----------------------------------------------------------===//

namespace {
/// Sends stdout to the null device while the core runs (its simplifiers
/// report progress and diagnostics with printf).
struct Silence {
  explicit Silence(bool on) {
    if (!on)
      return;
    std::fflush(stdout);
    llvm::outs().flush();
    saved = SIMBA_DUP(1);
    int nul = SIMBA_OPEN_NULL();
    if (saved < 0 || nul < 0) {
      if (nul >= 0)
        SIMBA_CLOSE(nul);
      return;
    }
    SIMBA_DUP2(nul, 1);
    SIMBA_CLOSE(nul);
  }
  ~Silence() {
    if (saved < 0)
      return;
    std::fflush(stdout);
    llvm::outs().flush();
    SIMBA_DUP2(saved, 1);
    SIMBA_CLOSE(saved);
  }
  int saved = -1;
};

/// Selects SiMBA's --simplifier for one call and restores it.
struct RouteScope {
  explicit RouteScope(Route r) : saved(SimplifierChoice.getValue()) {
    const char *name = "auto";
    switch (r) {
    case Route::Auto: name = "auto"; break;
    case Route::Linear: name = "native"; break;
    case Route::General: name = "general"; break;
    case Route::Multibit: name = "msimba"; break;
    }
    SimplifierChoice.setValue(name);
  }
  ~RouteScope() { SimplifierChoice.setValue(saved); }
  std::string saved;
};
} // namespace

Result simplify(const Expr &e, const Options &options) {
  Result r;
  if (!e) {
    r.status = Status::Unsupported;
    return r;
  }
  unsigned width = e.bitWidth();
  if (width == 0 || width > 64) {
    r.status = Status::Unsupported;
    return r;
  }
  std::string in = e.str();
  std::string out;
  Silence quiet(options.quiet);
  RouteScope scope(options.route);
  LSiMBA::RouteResult route = LSiMBA::RouteSimplify(
      in, out, int(width), options.prove, /*fastCheck=*/true, options.parallel,
      /*checkLinear=*/true, /*autoFallback=*/false);

  switch (route) {
  case LSiMBA::RouteResult::SKIPPED:
    r.status = Status::Skipped;
    return r;
  case LSiMBA::RouteResult::FAILED:
    r.status = Status::NotSimplified;
    return r;
  case LSiMBA::RouteResult::INVALID:
    r.status = Status::Invalid;
    return r;
  case LSiMBA::RouteResult::SUCCESS:
    r.guarantee = options.prove ? Guarantee::Proven : Guarantee::Sampled;
    break;
  case LSiMBA::RouteResult::NATIVE: {
    // Linear MBA: the truth-table method is exact once the input is checked
    // to be linear.
    LSiMBA::MBAChecker checker;
    if (!checker.check_expression(in)) {
      r.status = Status::NotSimplified;
      return r;
    }
    if (!LSiMBA::Simplifier::simplify_linear_mba(
            in, out, int(width), options.prove, /*checkLinear=*/false,
            /*fastCheck=*/true, options.parallel) ||
        out.empty()) {
      r.status = Status::NotSimplified;
      return r;
    }
    r.guarantee = options.prove ? Guarantee::Proven : Guarantee::Construction;
    break;
  }
  }

  std::shared_ptr<Node> tree = LSiMBA::MBA::parse(out, int(width));
  if (!tree) {
    r.status = Status::Invalid;
    r.guarantee = Guarantee::None;
    return r;
  }
  r.status = Status::Simplified;
  r.expr = Expr::fromRaw(tree);
  return r;
}

namespace {
/// `e` as a Z3 bit-vector term (tree-native; no string round trip).
z3::expr toZ3(z3::context &ctx, const Expr &e,
              std::map<unsigned, z3::expr> &vars) {
  unsigned w = e.bitWidth();
  auto op = [&](unsigned i) { return toZ3(ctx, e.operand(i), vars); };
  auto fold = [&](auto f) {
    z3::expr acc = op(0);
    for (unsigned i = 1; i < e.numOperands(); ++i)
      acc = f(acc, op(i));
    return acc;
  };
  switch (e.kind()) {
  case Kind::Constant:
    return ctx.bv_val(
        std::to_string(e.constant().zextOrTrunc(w).getZExtValue()).c_str(), w);
  case Kind::Variable: {
    auto it = vars.find(e.variable());
    if (it == vars.end())
      it = vars
               .emplace(e.variable(),
                        ctx.bv_const(("x" + std::to_string(e.variable())).c_str(),
                                     w))
               .first;
    return it->second;
  }
  case Kind::Add: return fold([](z3::expr a, z3::expr b) { return a + b; });
  case Kind::Mul: return fold([](z3::expr a, z3::expr b) { return a * b; });
  case Kind::And: return fold([](z3::expr a, z3::expr b) { return a & b; });
  case Kind::Or: return fold([](z3::expr a, z3::expr b) { return a | b; });
  case Kind::Xor: return fold([](z3::expr a, z3::expr b) { return a ^ b; });
  case Kind::Not: return ~op(0);
  case Kind::LShr: return z3::lshr(op(0), op(1));
  case Kind::UDiv: return z3::udiv(op(0), op(1));
  case Kind::URem: return z3::urem(op(0), op(1));
  case Kind::Pow: {
    uint64_t k = e.operand(1).constant().getZExtValue();
    z3::expr base = op(0), acc = ctx.bv_val(1, w);
    for (uint64_t i = 0; i < k && i < 64; ++i)
      acc = acc * base;
    return acc;
  }
  }
  return ctx.bv_val(0, w);
}
} // namespace

namespace {
/// Polynomial over "atoms" (conjunctions of variables, as variable bit
/// masks) with coefficients mod 2^w; a monomial is a sorted multiset of
/// atoms, the empty monomial is the constant term.
using Monomial = std::vector<uint32_t>;
using Poly = std::map<Monomial, llvm::APInt>;

struct Algebra {
  unsigned w;
  size_t maxTerms = 20000;
  bool failed = false;

  void addTerm(Poly &p, const Monomial &m, const llvm::APInt &c) {
    if (c.isZero())
      return;
    auto [it, inserted] = p.try_emplace(m, c);
    if (!inserted) {
      it->second += c;
      if (it->second.isZero())
        p.erase(it);
    }
    if (p.size() > maxTerms)
      failed = true;
  }
  Poly constant(const llvm::APInt &c) {
    Poly p;
    addTerm(p, {}, c);
    return p;
  }
  Poly add(const Poly &a, const Poly &b) {
    Poly r = a;
    for (auto &[m, c] : b)
      addTerm(r, m, c);
    return r;
  }
  Poly mul(const Poly &a, const Poly &b) {
    Poly r;
    for (auto &[ma, ca] : a)
      for (auto &[mb, cb] : b) {
        if (failed)
          return r;
        Monomial m = ma;
        m.insert(m.end(), mb.begin(), mb.end());
        std::sort(m.begin(), m.end());
        addTerm(r, m, ca * cb);
      }
    return r;
  }

  /// Variables of a purely bitwise tree (constants only 0 and -1), or
  /// false if it is not one.
  bool bitwiseVars(const Expr &e, std::vector<unsigned> &vars) {
    switch (e.kind()) {
    case Kind::Variable:
      if (!llvm::is_contained(vars, e.variable()))
        vars.push_back(e.variable());
      return true;
    case Kind::Constant:
      return e.constant().isZero() || e.constant().isAllOnes();
    case Kind::Not:
    case Kind::And:
    case Kind::Or:
    case Kind::Xor:
      for (unsigned i = 0; i < e.numOperands(); ++i)
        if (!bitwiseVars(e.operand(i), vars))
          return false;
      return true;
    default:
      return false;
    }
  }
  /// One bit of a purely bitwise tree under a 0/1 assignment.
  bool evalBit(const Expr &e, const std::vector<unsigned> &vars,
               uint32_t assign) {
    switch (e.kind()) {
    case Kind::Variable: {
      auto pos = llvm::find(vars, e.variable()) - vars.begin();
      return (assign >> pos) & 1;
    }
    case Kind::Constant:
      return e.constant().isAllOnes();
    case Kind::Not:
      return !evalBit(e.operand(0), vars, assign);
    case Kind::And: {
      bool r = true;
      for (unsigned i = 0; i < e.numOperands(); ++i)
        r = r && evalBit(e.operand(i), vars, assign);
      return r;
    }
    case Kind::Or: {
      bool r = false;
      for (unsigned i = 0; i < e.numOperands(); ++i)
        r = r || evalBit(e.operand(i), vars, assign);
      return r;
    }
    case Kind::Xor: {
      bool r = false;
      for (unsigned i = 0; i < e.numOperands(); ++i)
        r = r != evalBit(e.operand(i), vars, assign);
      return r;
    }
    default:
      return false;
    }
  }

  /// A bitwise tree as a linear combination of conjunctions (Moebius
  /// inversion of its truth table); the empty conjunction is all ones, -1.
  Poly linearize(const Expr &e, const std::vector<unsigned> &vars) {
    unsigned k = vars.size();
    std::vector<int64_t> coef(size_t(1) << k);
    for (uint32_t s = 0; s < (1u << k); ++s) {
      int64_t c = 0;
      for (uint32_t t = s;; t = (t - 1) & s) {
        int sign = (__builtin_popcount(s ^ t) & 1) ? -1 : 1;
        c += sign * int64_t(evalBit(e, vars, t));
        if (!t)
          break;
      }
      coef[s] = c;
    }
    Poly p;
    for (uint32_t s = 0; s < (1u << k); ++s) {
      if (!coef[s])
        continue;
      llvm::APInt c(w, uint64_t(coef[s]), /*isSigned=*/true);
      if (!s) {
        addTerm(p, {}, -c); // the all-ones conjunction is -1
        continue;
      }
      uint32_t mask = 0;
      for (unsigned i = 0; i < k; ++i)
        if ((s >> i) & 1)
          mask |= 1u << vars[i];
      addTerm(p, {mask}, c);
    }
    return p;
  }

  Poly canonical(const Expr &e) {
    if (failed)
      return {};
    switch (e.kind()) {
    case Kind::Constant:
      return constant(e.constant().zextOrTrunc(w));
    case Kind::Variable:
      if (e.variable() >= 32) {
        failed = true;
        return {};
      }
      return Poly{{Monomial{1u << e.variable()}, llvm::APInt(w, 1)}};
    case Kind::Add: {
      Poly r;
      for (unsigned i = 0; i < e.numOperands(); ++i)
        r = add(r, canonical(e.operand(i)));
      return r;
    }
    case Kind::Mul: {
      Poly r = constant(llvm::APInt(w, 1));
      for (unsigned i = 0; i < e.numOperands(); ++i)
        r = mul(r, canonical(e.operand(i)));
      return r;
    }
    case Kind::Pow: {
      uint64_t k = e.operand(1).constant().getZExtValue();
      if (k > 16) {
        failed = true;
        return {};
      }
      Poly base = canonical(e.operand(0)), r = constant(llvm::APInt(w, 1));
      for (uint64_t i = 0; i < k; ++i)
        r = mul(r, base);
      return r;
    }
    case Kind::Not:
    case Kind::And:
    case Kind::Or:
    case Kind::Xor: {
      std::vector<unsigned> vars;
      if (!bitwiseVars(e, vars) || vars.size() > 12) {
        failed = true;
        return {};
      }
      return linearize(e, vars);
    }
    default:
      failed = true;
      return {};
    }
  }
};
} // namespace

bool proveEquivalent(const Expr &a, const Expr &b, unsigned timeoutMs) {
  if (!a || !b || a.bitWidth() != b.bitWidth() || a.bitWidth() > 64)
    return false;
  // Exact algebra first: both sides as polynomials over conjunctions of
  // variables. Equal polynomials mean equal functions (the converse need
  // not hold, so a difference falls through to Z3).
  {
    Algebra alg{a.bitWidth()};
    Poly pa = alg.canonical(a), pb = alg.canonical(b);
    if (!alg.failed && pa == pb)
      return true;
  }
  try {
    z3::context ctx;
    std::map<unsigned, z3::expr> vars;
    z3::expr ea = toZ3(ctx, a, vars), eb = toZ3(ctx, b, vars);
    z3::solver solver(ctx, "QF_BV");
    z3::params p(ctx);
    if (timeoutMs)
      p.set("timeout", timeoutMs);
    solver.set(p);
    solver.add(ea != eb);
    return solver.check() == z3::unsat;
  } catch (const z3::exception &) {
    return false;
  }
}

} // namespace simba
