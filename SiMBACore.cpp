// simba/Core.h implementation: expression trees over LSiMBA::MBA::Node and
// simplify() on top of the router and the native linear simplifier.
#include "simba/Core.h"

#include "MBA/Node.h"
#include "MBA/Parser.h"
#include "Simplifier.h"
#include "SimplifierRouter.h"

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

} // namespace simba
