// MSiMBA semi-linear equivalence prover by signature lifting.
// See SemiLinearProver.h for the theorem, the measured rationale, and the
// safety contract.
#include "SemiLinearProver.h"

#include <map>
#include <utility>

#ifdef MBA_HAS_Z3
#include <z3++.h>

#include "ShuttingYard.h"

namespace LSiMBA {
namespace MBA {

namespace {

std::string symstr(z3::symbol s) {
  if (s.kind() == Z3_STRING_SYMBOL)
    return s.str();
  return "$" + std::to_string(s.to_int());
}

// True if e is a numeral: a real numeral AST or a 0-arg app printed as
// "#xHHH"/"#bBBB"/decimal (the project parser emits numerals as 0-arg
// apps in this Z3 build; is_const() alone is unreliable because declared
// variables are also 0-arg apps).
bool gNumeral(z3::expr e, uint64_t &v) {
  if (e.is_numeral_u64(v))
    return true;
  if (e.is_app() && e.num_args() == 0) {
    std::string n = e.to_string();
    if (n.size() > 2 && n[0] == '#') {
      bool hex = n[1] == 'x' || n[1] == 'X';
      bool bin = n[1] == 'b' || n[1] == 'B';
      if (!hex && !bin)
        return false;
      v = 0;
      for (size_t i = 2; i < n.size(); ++i) {
        if (bin) {
          if (n[i] != '0' && n[i] != '1')
            return false;
          v = (v << 1) | (n[i] == '1');
        } else {
          char ch = n[i];
          int d = ch >= '0' && ch <= '9'            ? ch - '0'
                   : ch >= 'a' && ch <= 'f'         ? ch - 'a' + 10
                   : ch >= 'A' && ch <= 'F'         ? ch - 'A' + 10
                   : -1;
          if (d < 0)
            return false;
          v = (v << 4) | (uint64_t)d;
        }
      }
      return true;
    }
    if (!n.empty()) {
      bool alldec = true;
      for (char ch : n)
        if (ch < '0' || ch > '9') {
          alldec = false;
          break;
        }
      if (alldec) {
        v = 0;
        for (char ch : n)
          v = v * 10 + (uint64_t)(ch - '0');
        return true;
      }
    }
  }
  return false;
}

// ---- Semi-linear class check (precondition for the lifting theorem) ----
// Base class: variables and numerals under bvnot/bvand/bvor/bvxor/
// bvadd/bvsub/bvneg/shl(const). The full class adds bvmul/bvsmul with a
// numeral on one side (recursively) and bvadd/bvsub of full-class terms.
bool semiBase(z3::expr e) {
  if (e.is_app() && e.num_args() == 0)
    return true; // var or numeral
  std::string op = symstr(e.decl().name());
  if (op == "bvnot") {
    z3::expr a = e.arg(0);
    return semiBase(a);
  }
  if (op == "shl") {
    z3::expr a = e.arg(0), b = e.arg(1);
    uint64_t bv;
    return semiBase(a) && gNumeral(b, bv);
  }
  if (op == "bvand" || op == "bvor" || op == "bvxor" || op == "bvadd" ||
      op == "bvsub" || op == "bvneg") {
    for (unsigned i = 0; i < e.num_args(); ++i) {
      z3::expr a = e.arg(i);
      if (!semiBase(a))
        return false;
    }
    return true;
  }
  return false;
}

bool semiLin(z3::expr e) {
  if (semiBase(e))
    return true;
  std::string op = symstr(e.decl().name());
  if (op == "bvmul" || op == "bvsmul") {
    z3::expr a = e.arg(0), b = e.arg(1);
    uint64_t av, bv;
    if (gNumeral(a, av))
      return semiLin(b);
    if (gNumeral(b, bv))
      return semiLin(a);
    return false;
  }
  if (op == "bvadd" || op == "bvsub") {
    z3::expr a = e.arg(0), b = e.arg(1);
    return semiLin(a) && semiLin(b);
  }
  return false;
}

// ---- Direct N-bit evaluation of a parsed BV tree ----
// One walk per point with concrete values; memoized by AST id (the tree is
// a DAG — naive recursion would be exponential on carry chains).
bool evalBVDirect(z3::expr e, const std::vector<std::string> &vars,
                  const std::vector<uint64_t> &vals, int N,
                  std::map<unsigned, uint64_t> &memo, uint64_t &out) {
  auto mit = memo.find(e.id());
  if (mit != memo.end()) {
    out = mit->second;
    return true;
  }
  out = 0;
  uint64_t mask = N >= 64 ? ~0ULL : ((1ULL << N) - 1);
  bool done = false;
  if (e.is_app() && e.num_args() == 0) {
    std::string name = e.to_string();
    uint64_t v;
    if (gNumeral(e, v)) {
      out = v & mask;
      done = true;
    } else {
      size_t us = name.rfind('_');
      std::string vn = (us != std::string::npos && us + 1 < name.size() &&
                        name[us + 1] >= '0' && name[us + 1] <= '9')
                           ? name.substr(0, us)
                           : name;
      for (size_t i = 0; i < vars.size(); ++i)
        if (vars[i] == vn) {
          out = vals[i] & mask;
          done = true;
          break;
        }
    }
  } else if (e.is_app()) {
    std::string n = symstr(e.decl().name());
    unsigned na = e.num_args();
    auto argval = [&](unsigned i, bool &ok) -> uint64_t {
      z3::expr a = e.arg(i);
      uint64_t v = 0;
      ok = evalBVDirect(a, vars, vals, N, memo, v);
      return v;
    };
    bool ok = true;
    if (n == "bvnot" && na == 1) {
      out = ~argval(0, ok) & mask;
      done = true;
    } else if (n == "bvadd" || n == "bvor" || n == "bvand" || n == "bvxor") {
      uint64_t a = argval(0, ok);
      for (unsigned k = 1; k < na && ok; ++k) {
        uint64_t b = argval(k, ok);
        if (n == "bvadd") {
          a = (a + b) & mask;
          break;
        } else if (n == "bvor")
          a |= b;
        else if (n == "bvand")
          a &= b;
        else
          a ^= b;
      }
      out = a & mask;
      done = true;
    } else if (n == "bvsub" && na == 2) {
      out = (argval(0, ok) - argval(1, ok)) & mask;
      done = true;
    } else if (n == "bvneg" && na == 1) {
      out = (0 - argval(0, ok)) & mask;
      done = true;
    } else if ((n == "bvmul" || n == "bvsmul") && na == 2) {
      __uint128_t p = (__uint128_t)argval(0, ok) * (__uint128_t)argval(1, ok);
      out = (uint64_t)(p & mask);
      done = true;
    } else if (n == "shl" && na == 2) {
      uint64_t a = argval(0, ok), b = argval(1, ok);
      out = (b >= (uint64_t)N) ? 0 : ((a << b) & mask);
      done = true;
    } else if (n == "lshr" && na == 2) {
      uint64_t a = argval(0, ok), b = argval(1, ok);
      out = (b >= (uint64_t)N) ? 0 : (a >> b);
      done = true;
    } else if (n == "ashr" && na == 2) {
      // The dataset domain is unsigned; treat as logical shift.
      uint64_t a = argval(0, ok), b = argval(1, ok);
      out = (b >= (uint64_t)N) ? (a >> (N - 1)) : (a >> b);
      done = true;
    }
    done = done && ok;
  }
  if (!done)
    return false; // unrecognized node or bad leaf: caller abstains
  memo[e.id()] = out;
  return true;
}

} // namespace

bool isSemiLinearClass(const std::string &expr, int bitCount,
                       const std::vector<std::string> &vars) {
  z3::context c;
  std::map<std::string, llvm::Type *> varTypes;
  std::map<std::string, z3::expr *> varMap;
  std::string q = expr;
  std::vector<std::string> vcopy = vars;
  z3::expr e = getZ3ExprFromString(c, q, bitCount, vcopy, varTypes, varMap);
  return semiLin(e);
}

int proveSemiLinear(const std::string &e0, const std::string &e1,
                    int bitCount, const std::vector<std::string> &vars,
                    unsigned *differingOut) {
  if (differingOut)
    *differingOut = 0;
  if (bitCount < 1 || bitCount > 64 || vars.empty())
    return 2;

  z3::context c;
  std::map<std::string, llvm::Type *> varTypes;
  std::map<std::string, z3::expr *> varMap;
  std::string q0 = e0, q1 = e1;
  std::vector<std::string> vcopy = vars;
  z3::expr s0 = getZ3ExprFromString(c, q0, bitCount, vcopy, varTypes, varMap);
  z3::expr s1 = getZ3ExprFromString(c, q1, bitCount, vcopy, varTypes, varMap);

  if (!semiLin(s0) || !semiLin(s1))
    return 2; // abstain: class precondition not met

  unsigned differing = 0;
  const unsigned t = (unsigned)vars.size();
  for (int i = 0; i < bitCount; ++i) {
    for (uint64_t comb = 0; comb < (1ULL << t); ++comb) {
      std::vector<uint64_t> vals(vars.size(), 0);
      for (size_t v = 0; v < vars.size(); ++v)
        vals[v] = (((comb >> v) & 1) << i);
      std::map<unsigned, uint64_t> m0, m1;
      uint64_t v0 = 0, v1 = 0;
      if (!evalBVDirect(s0, vars, vals, bitCount, m0, v0) ||
          !evalBVDirect(s1, vars, vals, bitCount, m1, v1))
        return 2; // evaluation failure: abstain (never prove on a partial
      if ((v0 >> i) != (v1 >> i) && ++differing == 16 && differingOut)
        *differingOut = differing;
    }
  }
  if (differingOut)
    *differingOut = differing < 16 ? differing : 16;
  return differing == 0 ? 1 : 0;
}

} // namespace MBA
} // namespace LSiMBA

#else // !MBA_HAS_Z3

namespace LSiMBA {
namespace MBA {

bool isSemiLinearClass(const std::string &expr, int bitCount,
                       const std::vector<std::string> &vars) {
  (void)expr;
  (void)bitCount;
  (void)vars;
  return false; // no Z3 backend: cannot parse to the AST class check
}

int proveSemiLinear(const std::string &e0, const std::string &e1,
                    int bitCount, const std::vector<std::string> &vars,
                    unsigned *differingOut) {
  (void)e0;
  (void)e1;
  (void)bitCount;
  (void)vars;
  if (differingOut)
    *differingOut = 0;
  return 2; // abstain
}

} // namespace MBA
} // namespace LSiMBA

#endif
