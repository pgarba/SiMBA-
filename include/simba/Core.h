//===- simba/Core.h - SiMBA as a library -----------------------*- C++ -*-===//
//
// The SiMBA simplifiers behind a front-end neutral API: callers build an
// expression tree with ExprBuilder (from LLVM IR, MLIR or anything else),
// call simplify(), and walk the result to rebuild it in their own IR.
//
// Expressions are over fixed-width integers (1..64 bits), with wrap-around
// arithmetic: +, -, *, ** (by a constant), ~ (bitwise not), &, |, ^ and the
// unsigned >> / % operations SiMBA knows. Variables are numbered; the
// caller keeps the mapping to its own values.
//
// The core today exchanges SiMBA's expression syntax internally; this API
// hides that, so it can become tree-native without changing callers.
//
// The selected simplifier is process-global state (SiMBA's --simplifier
// option), so simplify() is not reentrant across threads.
//
//===----------------------------------------------------------------------===//

#ifndef SIMBA_CORE_H
#define SIMBA_CORE_H

#include "llvm/ADT/APInt.h"

#include <cstdint>
#include <memory>
#include <string>

namespace LSiMBA::MBA {
class Node;
} // namespace LSiMBA::MBA

namespace simba {

enum class Kind {
  Constant,
  Variable,
  Add,     // n-ary sum
  Mul,     // n-ary product
  Pow,     // a ** constant
  Not,     // bitwise not
  And,
  Or,
  Xor,
  LShr,    // logical shift right
  UDiv,
  URem,
};

/// An immutable expression (shared subtrees allowed).
class Expr {
public:
  Expr() = default;
  explicit operator bool() const { return node != nullptr; }

  Kind kind() const;
  unsigned numOperands() const;
  Expr operand(unsigned i) const;
  /// Constant value at the expression's width (Kind::Constant).
  llvm::APInt constant() const;
  /// Variable number (Kind::Variable).
  unsigned variable() const;
  unsigned bitWidth() const;
  /// SiMBA syntax, variables printed as x0, x1, ...
  std::string str() const;

  /// The underlying SiMBA node, for code inside SiMBA.
  const std::shared_ptr<LSiMBA::MBA::Node> &raw() const { return node; }
  static Expr fromRaw(std::shared_ptr<LSiMBA::MBA::Node> n) {
    Expr e;
    e.node = std::move(n);
    return e;
  }

private:
  std::shared_ptr<LSiMBA::MBA::Node> node;
};

/// Builds expressions of one bit width.
class ExprBuilder {
public:
  explicit ExprBuilder(unsigned bitWidth);
  unsigned bitWidth() const { return width; }

  Expr constant(const llvm::APInt &v);
  Expr constant(uint64_t v);
  Expr variable(unsigned index);

  Expr add(Expr a, Expr b);
  Expr sub(Expr a, Expr b);
  Expr mul(Expr a, Expr b);
  Expr neg(Expr a);                // -a
  Expr pow(Expr a, uint64_t exponent);
  Expr bitNot(Expr a);
  Expr bitAnd(Expr a, Expr b);
  Expr bitOr(Expr a, Expr b);
  Expr bitXor(Expr a, Expr b);
  Expr shl(Expr a, unsigned amount); // a * 2^amount
  Expr lshr(Expr a, Expr b);
  Expr udiv(Expr a, Expr b);
  Expr urem(Expr a, Expr b);

  /// `e` rebuilt at this builder's width; constants are sign-extended or
  /// truncated (a coefficient of -1 stays -1).
  Expr import(const Expr &e);

private:
  Expr binary(Kind k, Expr a, Expr b);
  unsigned width;
};

enum class Route {
  Auto,      // SiMBA's classification (linear -> native, else general/msimba)
  Linear,    // native linear SiMBA
  General,   // GAMBA port (general/nonlinear)
  Multibit,  // MSiMBA (semi-linear)
};

/// How a result is known to be equivalent to the input.
enum class Guarantee {
  None,
  Sampled,      // agreed on random inputs only
  Construction, // linear MBA via the truth-table method: exact
  Proven,       // Z3 proved equivalence
};

enum class Status {
  Simplified,    // `expr` is set
  NotSimplified, // no simplifier produced a result
  Skipped,       // gated out (too many variables, too small)
  Invalid,       // a result failed verification and was dropped
  Unsupported,   // the input uses something the core cannot take
};

struct Options {
  Route route = Route::Auto;
  /// Prove results with Z3 (Guarantee::Proven) instead of sampling.
  bool prove = false;
  /// Let the native simplifier use threads.
  bool parallel = false;
  /// Silence the core's printing to stdout during the call.
  bool quiet = true;
};

struct Result {
  Status status = Status::NotSimplified;
  Guarantee guarantee = Guarantee::None;
  Expr expr;
};

/// Simplifies `e`. The result uses the same variable numbers.
Result simplify(const Expr &e, const Options &options = {});

/// Proves `a == b` for all inputs at their (common) width with Z3 (the trees
/// are encoded directly as bit-vector terms). False if not proven: different,
/// unknown within `timeoutMs` (0: no limit), or the widths differ.
bool proveEquivalent(const Expr &a, const Expr &b, unsigned timeoutMs = 10000);

} // namespace simba

#endif
