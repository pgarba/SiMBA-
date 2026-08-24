// GAMBA native C++ port — small CLI front-end for differential testing.
//
// Usage:
//   mba_cli parse   <bitCount> <expr>  -> prints parse(expr, bitCount).to_string()
//   mba_cli dump    <bitCount> <expr>  -> prints a structural dump of the AST
//   mba_cli state   <bitCount> <expr>  -> parse + markLinear + dump with states
//   mba_cli stats   <bitCount> <expr>  -> prints node/alternation/term counts
//
// On parse failure, prints "ERROR: <message>".
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "BitwiseFactory.h"
#include "GeneralSimplifier.h"
#include "LinearSimplifier.h"
#include "MultibitSimplifier.h"
#include "Node.h"
#include "Parser.h"
#include "Verify.h"

using namespace LSiMBA::MBA;

static void dumpNode(const Node &n, int level) {
  std::string indent(2 * level, ' ');
  printf("%s[%d] %d", indent.c_str(), level, static_cast<int>(n.type));
  if (n.type == NodeType::CONSTANT)
    printf(" const=%s", MBAOps::toStringSigned(n.constant).c_str());
  if (n.type == NodeType::VARIABLE)
    printf(" var=%s vidx=%d", n.vname.c_str(), n.vidx);
  printf("\n");
  for (const auto &c : n.children)
    dumpNode(*c, level + 1);
}

// Structural dump including the mark_linear state and linearEnd, for
// differential testing against the Python oracle.
static void dumpState(const Node &n, int level) {
  std::string indent(2 * level, ' ');
  printf("%sT%d S%d LE%d", indent.c_str(), static_cast<int>(n.type),
         static_cast<int>(n.state), n.linearEnd);
  if (n.type == NodeType::CONSTANT)
    printf(" C%s", MBAOps::toStringSigned(n.constant).c_str());
  if (n.type == NodeType::VARIABLE)
    printf(" V%s I%d", n.vname.c_str(), n.vidx);
  printf("\n");
  for (const auto &c : n.children)
    dumpState(*c, level + 1);
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: mba_cli <parse|dump> <bitCount> <expr>\n");
    return 2;
  }

  std::string mode = argv[1];
  int bitCount = atoi(argv[2]);
  if (argc < 4) {
    fprintf(stderr, "missing expression\n");
    return 2;
  }
  std::string expr = argv[3];

  if (mode == "bitwise") {
    // mba_cli bitwise <vnumber> <comma-sep truth values>
    if (argc < 4) {
      fprintf(stderr, "bitwise needs vnumber and truth values\n");
      return 2;
    }
    int vnumber = bitCount;
    std::vector<int> vec;
    std::stringstream ss(expr);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
      if (!tok.empty())
        vec.push_back(atoi(tok.c_str()));
    }
    BitwiseFactory f(vnumber);
    printf("%s\n", f.createBitwise(vec).c_str());
    return 0;
  }

  if (mode == "verify" || mode == "prove") {
    // mba_cli <verify|prove> <bitCount> <orig> <simp>
    //   verify -> fast-check (random-value evaluation, no Z3)
    //   prove  -> Z3 proof
    // Prints "EQUIVALENT"/"NOT EQUIVALENT" (verify) or
    // "PROVED"/"NOT PROVED" (prove).
    if (argc < 5) {
      fprintf(stderr, "%s needs bitCount, orig and simp\n", mode.c_str());
      return 2;
    }
    std::string simp = argv[4];
    bool ok = mode == "verify"
                  ? fastCheckEquivalent(expr, simp, bitCount)
                  : proveEquivalent(expr, simp, bitCount);
    if (mode == "verify")
      printf("%s\n", ok ? "EQUIVALENT" : "NOT EQUIVALENT");
    else
      printf("%s\n", ok ? "PROVED" : "NOT PROVED");
    return ok ? 0 : 1;
  }

  if (mode == "generalbatch") {
    // mba_cli generalbatch <bitCount> <file>
    // Reads expressions from <file> (one per line; blank and '#' lines
    // skipped) and prints one simplified result per line (empty line on
    // failure). All lines are processed in a single process, avoiding the
    // per-expression process start-up of the single-expression mode.
    std::ifstream in(expr);
    if (!in) {
      fprintf(stderr, "cannot open %s\n", expr.c_str());
      return 1;
    }
    std::string line;
    while (std::getline(in, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ' ||
                               line.back() == '\t'))
        line.pop_back();
      if (line.empty() || line[0] == '#')
        continue;
      std::string res = simplifyMba(line, bitCount, false, false, -1);
      printf("%s\n", res.c_str());
    }
    return 0;
  }

  auto root = parse(expr, bitCount, true, false, false);
  if (root == nullptr) {
    // Re-run to capture the error message.
    Parser p(expr, bitCount, true);
    p.parseExpression();
    printf("ERROR: %s\n", p.error().c_str());
    return 1;
  }

  if (mode == "parse") {
    printf("%s\n", root->toString().c_str());
  } else if (mode == "parsemr0") {
    // Raw parse with modRed=false (matches the simplify() path).
    auto r2 = parse(expr, bitCount, false, false, false);
    printf("%s\n", r2->toString().c_str());
  } else if (mode == "eval") {
    // mba_cli eval <bitCount> <expr> <v1,v2,...>
    // Prints the variable names (in enumeration order) then "RESULT <value>".
    std::vector<std::string> vars;
    root->collectAndEnumerateVariables(vars);
    for (size_t i = 0; i < vars.size(); ++i)
      printf("%s%s", vars[i].c_str(), i + 1 < vars.size() ? "," : "\n");
    std::vector<std::uint64_t> X;
    if (argc >= 5) {
      std::stringstream ss(argv[4]);
      std::string tok;
      while (std::getline(ss, tok, ','))
        if (!tok.empty())
          X.push_back(std::stoull(tok));
    }
    while (X.size() < vars.size())
      X.push_back(0);
    printf("RESULT %llu\n", (unsigned long long)root->eval(X));
  } else if (mode == "dump") {
    dumpNode(*root, 0);
  } else if (mode == "state") {
    root->markLinear();
    dumpState(*root, 0);
  } else if (mode == "stats") {
    printf("nodes=%d alt=%d altLin=%d terms=%d\n", root->countNodes(),
           root->computeAlternation(nullptr), root->computeAlternationLinear(false),
           root->isLinear() ? root->countTermsLinear() : -1);
  } else if (mode == "refine") {
    root->refine();
    printf("%s\n", root->toString().c_str());
  } else if (mode == "polish") {
    root->markLinear();
    root->polish();
    printf("%s\n", root->toString().c_str());
  } else if (mode == "refinedump") {
    root->refine();
    dumpNode(*root, 0);
  } else if (mode == "expand") {
    root->expand();
    printf("%s\n", root->toString().c_str());
  } else if (mode == "factorize") {
    root->markLinear();
    root->factorizeSums();
    printf("%s\n", root->toString().c_str());
  } else if (mode == "subst") {
    auto sub = root->getNodeForSubstitution({});
    if (sub != nullptr)
      root->substituteAllOccurences(sub, "t");
    printf("%s\n", root->toString().c_str());
  } else if (mode == "simplify") {
    std::string res = simplifyLinearMba(expr, bitCount, false, false, false, true, -1,
                                        Metric::ALTERNATION);
    printf("%s\n", res.c_str());
  } else if (mode == "general") {
    // mba_cli general <bitCount> <expr> -> prints simplifyMba(expr, bitCount).
    std::string res = simplifyMba(expr, bitCount, false, false, -1);
    printf("%s\n", res.c_str());
  } else if (mode == "msimba") {
    // mba_cli msimba <bitCount> <expr> -> prints MultibitSimplifier::simplify.
    std::string res = MultibitSimplifier::simplify(expr, bitCount, false);
    printf("%s\n", res.c_str());
  } else if (mode == "msimbacheck") {
    // mba_cli msimbacheck <bitCount> <expr> -> prints "semi-linear" or "linear".
    bool semi = MultibitSimplifier::isSemiLinear(expr);
    printf("%s\n", semi ? "semi-linear" : "linear");
  } else if (mode == "msimbavector") {
    // mba_cli msimbavector <bitCount> <expr> -> prints the multi-bit result vector.
    // Debug: prints the vector for the first 2 bits.
    auto ast = parse(expr, bitCount, false, false, false);
    if (!ast) { printf("parse error\n"); return 1; }
    std::vector<std::string> vars;
    ast->collectVariables(vars);
    ast->enumerateVariables(vars);
    int vc = vars.size();
    uint64_t nc = 1ull << vc;
    uint64_t mask = (bitCount >= 64) ? ~0ull : ((1ull << bitCount) - 1);
    printf("vars=%d numCombinations=%llu\n", vc, (unsigned long long)nc);
    for (uint32_t bi = 0; bi < 2 && bi < (uint32_t)bitCount; bi++) {
      printf("bit %u: ", bi);
      for (uint64_t c = 0; c < nc; c++) {
        std::vector<uint64_t> vals(vc, 0);
        for (int v = 0; v < vc; v++) {
          uint64_t vm = 1ull << v;
          uint64_t vv = (c & vm) >> v;
          vals[v] = vv << bi;
        }
        uint64_t ev = ast->eval(vals);
        ev = mask & ev;
        ev >>= bi;
        printf("%llu ", (unsigned long long)ev);
      }
      printf("\n");
    }
  } else {
    fprintf(stderr, "unknown mode %s\n", mode.c_str());
    return 2;
  }
  return 0;
}
