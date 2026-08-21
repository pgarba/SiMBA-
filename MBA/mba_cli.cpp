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
#include <sstream>
#include <string>
#include <vector>

#include "BitwiseFactory.h"
#include "GeneralSimplifier.h"
#include "LinearSimplifier.h"
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
  } else {
    fprintf(stderr, "unknown mode %s\n", mode.c_str());
    return 2;
  }
  return 0;
}
