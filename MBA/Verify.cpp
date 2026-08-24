// GAMBA native C++ port — result verification (fast-check + Z3 proof).
#include "Verify.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "Node.h"
#include "Parser.h"
#include "splitmix64.h"

// The Z3 proof path pulls in the project's Z3 backend (Z3Prover.cpp ->
// getZ3ExprFromString in ShuttingYard.cpp + the SiMBAOpt category), which the
// lightweight mba_cli differential tool does not link. Builds that link Z3
// define MBA_HAS_Z3 (see CMakeLists.txt); without it proveEquivalent is a
// safe no-op returning false.
#ifdef MBA_HAS_Z3
#include "Z3Prover.h"
#endif

namespace LSiMBA {
namespace MBA {

namespace {

// Union of the variables of both trees (original enumeration order first,
// then any variables new to the simplified side — which the fast-check
// then assigns random values, so a genuinely new variable is correctly
// rejected), with vidx set on both trees.
void enumerateUnion(const std::shared_ptr<Node> &a, const std::shared_ptr<Node> &b,
                    std::vector<std::string> &vars) {
  a->collectAndEnumerateVariables(vars);
  std::vector<std::string> vb;
  b->collectAndEnumerateVariables(vb);
  for (auto &v : vb)
    if (std::find(vars.begin(), vars.end(), v) == vars.end())
      vars.push_back(v);
  b->enumerateVariables(vars);
}

// Rewrite 0x…/0b… constants to decimal so the native Z3 tokenizer
// (getZ3ExprFromString in ShuttingYard.cpp) sees only decimal numbers,
// variables, and the operators it knows. GAMBA output is always decimal,
// so this only matters for the original user expression.
std::string normalizeForNativeTokenizer(const std::string &expr) {
  std::string out;
  out.reserve(expr.size());
  size_t i = 0;
  while (i < expr.size()) {
    if (expr[i] == '0' && i + 1 < expr.size() &&
        (expr[i + 1] == 'x' || expr[i + 1] == 'b')) {
      int base = expr[i + 1] == 'x' ? 16 : 2;
      size_t j = i + 2;
      uint64_t v = 0;
      size_t digits = 0;
      while (j < expr.size() &&
             std::isxdigit(static_cast<unsigned char>(expr[j]))) {
        int d = std::isdigit(static_cast<unsigned char>(expr[j]))
                    ? expr[j] - '0'
                    : std::tolower(static_cast<unsigned char>(expr[j])) - 'a' + 10;
        if (d >= base)
          break;
        v = v * static_cast<uint64_t>(base) + static_cast<uint64_t>(d);
        digits++;
        j++;
      }
      if (digits > 0) {
        out += std::to_string(static_cast<unsigned long long>(v));
        i = j;
        continue;
      }
    }
    out.push_back(expr[i]);
    i++;
  }
  return out;
}

} // namespace

bool fastCheckEquivalent(const std::string &orig, const std::string &simp,
                         int bitCount, int numSamples, bool quiet) {
  auto a = parse(orig, bitCount, true, false, false);
  if (a == nullptr) {
    if (!quiet)
      printf("[!] fast-check: could not parse '%s'\n", orig.c_str());
    return false;
  }
  auto b = parse(simp, bitCount, true, false, false);
  if (b == nullptr) {
    if (!quiet)
      printf("[!] fast-check: could not parse '%s'\n", simp.c_str());
    return false;
  }

  std::vector<std::string> vars;
  enumerateUnion(a, b, vars);
  int vnum = static_cast<int>(vars.size());

  // Deterministic splitmix64 sequence (fixed seed): reproducible across
  // runs, uniform values reduced to the bitCount-bit modulus.
  SplitMix64 rng(0x9E3779B97F4A7C15ULL);
  for (int i = 0; i < numSamples; ++i) {
    std::vector<uint64_t> par;
    par.reserve(vnum);
    for (int j = 0; j < vnum; ++j)
      par.push_back(MBAOps::reduce(rng.next(), bitCount));

    uint64_t r0 = a->eval(par);
    uint64_t r1 = b->eval(par);
    if (r0 != r1) {
      if (!quiet) {
        printf("[!] fast-check counterexample for '%s' vs '%s': ", orig.c_str(),
               simp.c_str());
        for (int j = 0; j < vnum; ++j)
          printf("%s=%llu ", vars[j].c_str(), (unsigned long long)par[j]);
        printf("=> %llu != %llu\n", (unsigned long long)r0, (unsigned long long)r1);
      }
      return false;
    }
  }
  return true;
}

bool proveEquivalent(const std::string &orig, const std::string &simp,
                     int bitCount) {
#ifndef MBA_HAS_Z3
  (void)orig;
  (void)simp;
  (void)bitCount;
  return false; // Z3 backend not linked in this build
#else
  // Both sides must be GAMBA-parseable for the Z3 encoding to be defined
  // here.
  auto a = parse(orig, bitCount, true, false, false);
  if (a == nullptr)
    return false;
  auto b = parse(simp, bitCount, true, false, false);
  if (b == nullptr)
    return false;

  std::vector<std::string> vars;
  enumerateUnion(a, b, vars);

  // Use the parsed (desugared) forms so both sides reference the same
  // variables. The raw original string may reference a plain variable (e.g.
  // `a`) that does not appear in the desugared form (which uses the bit-slice
  // variables `a[i]` introduced by the >>, /, % desugaring); feeding the raw
  // string to the native Z3 tokenizer would then see an unknown variable.
  std::string e0 = normalizeForNativeTokenizer(a->toString());
  std::string e1 = normalizeForNativeTokenizer(b->toString());
  return proveReplacement(e0, e1, bitCount, vars);
#endif
}

} // namespace MBA
} // namespace LSiMBA
