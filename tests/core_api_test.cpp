// Checks the simba/Core.h API: builds MBAs, simplifies them and compares
// the result with the input on random values (an evaluator independent of
// SiMBA's own checks).
#include "simba/Core.h"

#include <cstdio>
#include <functional>
#include <random>
#include <vector>

using simba::Expr;
using simba::Kind;
using llvm::APInt;

static APInt eval(const Expr &e, const std::vector<APInt> &vars) {
  unsigned w = e.bitWidth();
  auto op = [&](unsigned i) { return eval(e.operand(i), vars); };
  switch (e.kind()) {
  case Kind::Constant: return e.constant();
  case Kind::Variable: return vars.at(e.variable());
  case Kind::Add: {
    APInt r(w, 0);
    for (unsigned i = 0; i < e.numOperands(); ++i)
      r += op(i);
    return r;
  }
  case Kind::Mul: {
    APInt r(w, 1);
    for (unsigned i = 0; i < e.numOperands(); ++i)
      r *= op(i);
    return r;
  }
  case Kind::Pow: {
    APInt base = op(0), r(w, 1);
    for (uint64_t k = op(1).getZExtValue(); k; --k)
      r *= base;
    return r;
  }
  case Kind::Not: return ~op(0);
  case Kind::And: {
    APInt r = op(0);
    for (unsigned i = 1; i < e.numOperands(); ++i)
      r &= op(i);
    return r;
  }
  case Kind::Or: {
    APInt r = op(0);
    for (unsigned i = 1; i < e.numOperands(); ++i)
      r |= op(i);
    return r;
  }
  case Kind::Xor: {
    APInt r = op(0);
    for (unsigned i = 1; i < e.numOperands(); ++i)
      r ^= op(i);
    return r;
  }
  case Kind::LShr: return op(0).lshr(op(1).getLimitedValue(w));
  case Kind::UDiv: { APInt d = op(1); return d.isZero() ? APInt(w, 0) : op(0).udiv(d); }
  case Kind::URem: { APInt d = op(1); return d.isZero() ? op(0) : op(0).urem(d); }
  }
  return APInt(w, 0);
}

static bool sameOnRandomInputs(const Expr &a, const Expr &b, unsigned vars) {
  std::mt19937_64 rng(42);
  for (int t = 0; t < 2000; ++t) {
    std::vector<APInt> v;
    for (unsigned i = 0; i < vars; ++i)
      v.push_back(APInt(a.bitWidth(), rng(), /*isSigned=*/false,
                        /*implicitTrunc=*/true));
    if (eval(a, v) != eval(b, v))
      return false;
  }
  return true;
}

static int failures = 0;

static void check(const char *name, const Expr &in, unsigned vars,
                  simba::Guarantee minimum) {
  simba::Result r = simba::simplify(in);
  bool ok = r.status == simba::Status::Simplified && r.expr &&
            sameOnRandomInputs(in, r.expr, vars) &&
            int(r.guarantee) >= int(minimum);
  std::printf("%s %s: %s -> %s (status %d, guarantee %d)\n",
              ok ? "PASS" : "FAIL", name, in.str().c_str(),
              r.expr ? r.expr.str().c_str() : "-", int(r.status),
              int(r.guarantee));
  failures += !ok;
}

int main() {
  simba::ExprBuilder b(64);
  Expr x = b.variable(0), y = b.variable(1), z = b.variable(2);

  // Linear: (x ^ y) + 2 * (x & y) == x + y.
  check("linear", b.add(b.bitXor(x, y), b.mul(b.constant(2), b.bitAnd(x, y))),
        2, simba::Guarantee::Construction);

  // Linear with a constant and three variables:
  // (x | y) - (x & y) + 3 * (z & ~z) + 7 == (x ^ y) + 7.
  check("linear3",
        b.add(b.sub(b.bitOr(x, y), b.bitAnd(x, y)),
              b.add(b.mul(b.constant(3), b.bitAnd(z, b.bitNot(z))),
                    b.constant(7))),
        3, simba::Guarantee::Construction);

  // Nonlinear: (x & y) * (x | y) + (x & ~y) * (~x & y) == x * y. SiMBA's
  // general route runs up to 16 bits (at 64 bits it is reported as not
  // simplified).
  simba::ExprBuilder b16(16);
  Expr p = b16.variable(0), q = b16.variable(1);
  check("product16",
        b16.add(b16.mul(b16.bitAnd(p, q), b16.bitOr(p, q)),
                b16.mul(b16.bitAnd(p, b16.bitNot(q)),
                        b16.bitAnd(b16.bitNot(p), q))),
        2, simba::Guarantee::Sampled);

  // 32-bit: (x + y) - 2 * (x & y) == x ^ y.
  simba::ExprBuilder b32(32);
  Expr a = b32.variable(0), c = b32.variable(1);
  check("linear32",
        b32.sub(b32.add(a, c), b32.mul(b32.constant(2), b32.bitAnd(a, c))), 2,
        simba::Guarantee::Construction);

  // Nonlinear at 64 bits: simplify at 16, import the result back, prove it.
  {
    Expr in64 = b.add(b.mul(b.bitAnd(x, y), b.bitOr(x, y)),
                      b.mul(b.bitAnd(x, b.bitNot(y)), b.bitAnd(b.bitNot(x), y)));
    simba::Result r16 = simba::simplify(b16.import(in64));
    Expr back = r16.expr ? b.import(r16.expr) : Expr();
    bool ok = back && simba::proveEquivalent(in64, back) &&
              sameOnRandomInputs(in64, back, 2);
    std::printf("%s import+prove: %s -> %s\n", ok ? "PASS" : "FAIL",
                in64.str().c_str(), back ? back.str().c_str() : "-");
    failures += !ok;
    bool neq = !simba::proveEquivalent(b.add(x, y), b.sub(x, y)) &&
               // One product term missing: the algebra sees a difference
               // and Z3 cannot prove it either.
               !simba::proveEquivalent(b.mul(b.bitAnd(x, y), b.bitOr(x, y)),
                                       b.mul(x, y), 1000);
    std::printf("%s prove rejects x+y == x-y\n", neq ? "PASS" : "FAIL");
    failures += !neq;
  }

  // Wider than the core handles.
  simba::ExprBuilder b128(128);
  simba::Result wide = simba::simplify(b128.add(b128.variable(0), b128.variable(1)));
  bool wideOk = wide.status == simba::Status::Unsupported;
  std::printf("%s unsupported width\n", wideOk ? "PASS" : "FAIL");
  failures += !wideOk;

  std::printf("%d failures\n", failures);
  return failures != 0;
}
