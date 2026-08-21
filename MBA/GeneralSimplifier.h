// GAMBA native C++ port — general (nonlinear) MBA simplifier.
// Mirrors external/GAMBA/src/simplify_general.py (class GeneralSimplifier).
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "LinearSimplifier.h"
#include "Node.h"
#include "Parser.h"

namespace LSiMBA {
namespace MBA {

// The main simplification class which stores relevant parameters such as the
// number of bits.
class GeneralSimplifier {
 public:
  // timeoutSec bounds the wall-clock deadline inside simplify() (the Python
  // source uses a multiprocessing 30s timeout; the C++ port mirrors it with
  // a 25s deadline by default).
  GeneralSimplifier(int bitCount, bool modRed = false, int verifBitCount = -1,
                    int timeoutSec = 25);

  // Returns the number of variables occurring in the given expression.
  int getVariableCount(const std::string &expr);

  // Simplify the given MBA. Returns the simplified expression, or "" on
  // failure. (The Python source uses a multiprocessing 30s timeout; the C++
  // port runs directly and instead enforces a simple wall-clock deadline.)
  std::string simplify(const std::string &expr, bool useZ3 = false);

 private:
  int bitCount;
  uint64_t modulus;
  bool modRed;
  int verifBitCount;
  int timeoutSec = 25;
  int vnumber = 0;
  std::vector<std::string> variables;
  int maxIt = 100;

  // Wall-clock deadline mirroring the Python 30s timeout.
  std::chrono::steady_clock::time_point deadline;

  std::string getVname(int i) const;
  int64_t modRedInt(int64_t n) const;
  void collectAndEnumerateVariables(const std::shared_ptr<Node> &tree);
  std::vector<int64_t> getResultVector(const std::shared_ptr<Node> &node) const;
  std::vector<int> getGroupSizes() const;
  std::vector<std::vector<int>> getVariableCombinations() const;
  std::string getBasisExpression(int idx) const;
  bool areVariablesTrue(int n, const std::vector<int> &variables) const;
  void subtractCoefficient(std::vector<int64_t> &resultVector, int64_t coeff, int firstStart,
                           const std::vector<int> &variables, int groupsize);
  std::vector<int64_t> getLinearCombination(const std::shared_ptr<Node> &node);
  std::vector<uint64_t> getProductLinearCombination(const std::shared_ptr<Node> &node);
  std::vector<uint64_t> getPowerLinearCombination(const std::shared_ptr<Node> &node);
  bool trySimplifySumNonlinearPart(const std::shared_ptr<Node> &node);
  bool isCandidateForSimplificationInSum(const std::shared_ptr<Node> &node) const;
  std::vector<int> getIndicesOfSimpleNonlinearProductsInSum(const std::shared_ptr<Node> &node);
  bool simplifyNonlinearSubexpressionLinearPart(const std::shared_ptr<Node> &node);
  bool refactor(const std::shared_ptr<Node> &node);
  bool simplifyNonlinearSubexpressionStep(const std::shared_ptr<Node> &node,
                                          const std::shared_ptr<Node> &parent, bool noRefactor,
                                          bool noSubst);
  bool simplifyNonlinearSubexpression(const std::shared_ptr<Node> &node,
                                      const std::shared_ptr<Node> &parent, bool noRefactor = false,
                                      bool noSubst = false);
  bool simplifyLinearSubexpression(const std::shared_ptr<Node> &node);
  std::vector<std::shared_ptr<Node>> collectNodesForSubstitution(const std::shared_ptr<Node> &root);
  std::shared_ptr<Node> getSimplViaSubstitutionOfNodes(
      const std::shared_ptr<Node> &node, const std::vector<std::shared_ptr<Node>> &nodes,
      bool onlyFullMatch);
  bool isSecondMoreOrEquallyComplex(const std::shared_ptr<Node> &first,
                                   const std::shared_ptr<Node> &second) const;
  bool simplifyViaSubstitutionOfNodes(const std::shared_ptr<Node> &node,
                                      const std::vector<std::shared_ptr<Node>> &nodes,
                                      bool onlyFullMatch);
  bool simplifyViaSubstitutionForIndex(const std::shared_ptr<Node> &node,
                                       const std::vector<std::shared_ptr<Node>> &nodes, int index);
  bool simplifyViaSubstitution(const std::shared_ptr<Node> &node);
  bool simplifySubexpression(const std::shared_ptr<Node> &node,
                             const std::shared_ptr<Node> &parent, bool noRefactor = false,
                             bool noSubst = false);
  bool verifyUsingZ3(const std::string &orig, const std::string &simpl);
  bool checkVerify(const std::string &orig, const std::shared_ptr<Node> &simplTree);
};

// Simplify the given expression with the given number of bits.
std::string simplifyMba(const std::string &expr, int bitCount, bool useZ3 = false,
                        bool modRed = false, int verifBitCount = -1,
                        int timeoutSec = 25);

} // namespace MBA
} // namespace LSiMBA
