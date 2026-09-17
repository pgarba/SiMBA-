// GAMBA native C++ port — general (nonlinear) MBA simplifier.
// Mirrors external/GAMBA/src/simplify_general.py (class GeneralSimplifier).
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
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

  // Phase-1 substitution budget. The substitution search is a combinatorial
  // per-node subset enumeration that dominates the cost on large nonlinear
  // MBAs (e.g. 10-variable obfuscatorx: ~20s of ~22s, 500k+ subsets, with
  // heavy oscillation) for a small net size gain. CoBRA instead stops after a
  // single verified equivalent. We bound the substitution work per simplify()
  // call so the tail cost is bounded; whatever is applied before the budget
  // is exhausted is still a verified-equivalent rewrite. 0 disables the
  // budget (unbounded, the historical behavior). Tunable via
  // MBASIMBA_SUBST_BUDGET_MS (and MBASIMBA_SUBST_MAX_SUBSETS).
  double substBudgetMs = 0;     // 0 = unbounded
  long substBudgetSubsets = 0;  // 0 = unbounded
  double substTimeSpentMs = 0;  // accumulated across the simplify() call
  long substSubsetsSpent = 0;   // accumulated across the simplify() call

  // Wall-clock deadline mirroring the Python 30s timeout.
  std::chrono::steady_clock::time_point deadline;

  // A1: per-node fingerprint (toString) of the last refactor that found no
  // change. refactor() skips the expensive expand+factorize rebuild when the
  // node is byte-identical to such a fingerprint (expand+factorize are
  // idempotent, so the rebuild would be a no-op). Keyed by node address;
  // cleared at the start of each simplify() call.
  std::unordered_map<uintptr_t, std::string> noChangeFingerprint;

  // A3: lazily-created zero constant node, reused instead of parse("0", ...).
  std::shared_ptr<Node> zeroNode;

  // Phase-0 profiling (gated by MBASIMBA_PERF=1); prints to stderr.
  struct PerfCounters {
    bool enabled = false;
    long iters = 0, refactorCalls = 0, refactorSkips = 0;
    double tLinear = 0, tRefactor = 0, tSubst = 0, tTotal = 0;
    // B-i instrumentation: substitution enumeration stats. substCalls =
    // node-level simplifyViaSubstitution calls; subsetTried = candidate
    // subsets that passed the popcount filter; substFound = subsets whose
    // copy+substitute+re-simplify produced a non-null result;
    // substImproved = subsets accepted (strictly simpler / equal-complexity
    // gate passed, node mutated). ByPopcount[k] counts subsetTried for
    // popcount k+1 (k>=3 only; smaller popcounts always pass the filter).
    // NodesHist bins: candidate-count histogram [<=4, 5-9, 10-20, >20].
    long substCalls = 0, subsetTried = 0, substFound = 0, substImproved = 0;
        // B-iii: fixed-point loop terminations by reason.
    long loopNoChange = 0, loopCycle = 0, loopDeadline = 0, loopMaxIt = 0;
    // B-i: per-attempt cost breakdown (seconds).
    double tSubCopy = 0, tSubRefine = 0, tSubSimplify = 0;
    double tSubMech = 0; // substituteAllOccurences + replaceVariable + getMaxVname
    double tSubTail = 0; // trailing collectAndEnumerateVariables + refine
    double tSubAccept = 0; // accepted result: node->copy + refine + markLinear
    double tSubAccRefine = 0, tSubAccMarkLin = 0;
    long substWalkChanged = 0; // accepted passes where refineAfterSubstitution fired
    double tComplexity = 0; // isSecondMoreOrEquallyComplex
    double tLinearSub = 0; // simplifyLinearSubexpression total
    double tLinearMba = 0; // ... of which the string-level linear solver
    double tLinearParse = 0; // ... of which parse+copy of the result
    long subsetTriedByPop[4] = {0, 0, 0, 0};
    long subsetImprovedByPop[4] = {0, 0, 0, 0};
    long nodesHist[4] = {0, 0, 0, 0};
    long linearSubCalls = 0, linearSubChanged = 0, linearSubDupCalls = 0;
    long linearCacheHits = 0;
  };
  PerfCounters perf;

  // B-i: per-simplify() set of already-seen linear-subexpression strings
  // (duplicate-input rate of the string-level linear solver).
  std::set<std::string> linearSeen;
  // W1: memo for the string-level linear solver. Key = exact subexpression
  // string; value = the solver's output. The solver is a pure function of its
  // string (deterministic, stateless w.r.t. the tree), so reusing its output
  // for an identical key is sound. A cached value equal to the key is a fixed
  // point, so it lets us skip the solver AND the changed-comparison entirely.
  std::unordered_map<std::string, std::string> linearSimplifyMemo;
  bool linMemoEnabled = false;

  std::shared_ptr<Node> getZero();
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
