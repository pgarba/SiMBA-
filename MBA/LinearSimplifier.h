// GAMBA native C++ port — linear MBA simplifier.
// Mirrors external/GAMBA/src/simplify.py.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "BitwiseFactory.h"
#include "Node.h"

namespace LSiMBA {
namespace MBA {

// A decision metric for comparing possible solutions.
enum class Metric : int {
  ALTERNATION = 0,
  TERMS = 1,
  STRING = 2,
  BITWISE_NODES = 3,
  COUNT = 4,
};

// A decision on whether a result-vector entry is a negated/unnegated expression.
enum class Decision : int {
  NONE = 0,
  FIRST = 1,
  SECOND = 2,
  BOTH = 3,
};

// Returns true iff the given expression is a linear MBA.
bool checkLinear(const std::string &expr, int bitCount);

// Returns the number of terms in the given expression (assumed linear).
int countTerms(const std::string &expr);

// Returns a complexity penalty for the bitwise operations in the given node.
int computeBitwiseComplexity(const Node &root);

// The main simplification class.
class LinearSimplifier {
 public:
  LinearSimplifier(int bitCount, const std::string &expr, bool modRed = false,
                   bool refine = true, int verifBitCount = -1,
                   Metric metric = Metric::ALTERNATION);

  bool valid = false;

  // Simplify the expression. Returns the simplified expression, or "" on failure.
  std::string simplify(bool useZ3);

 private:
  std::string origExpr;
  std::vector<int> groupSizes = {1};
  int bitCount;
  int64_t modulus;
  bool modRed;
  bool refine;
  int verifBitCount;
  Metric metric;
  std::vector<std::string> variables;
  std::shared_ptr<Node> tree;
  int vnumber = 0;
  std::shared_ptr<BitwiseFactory> bitwiseFactory;
  std::vector<int64_t> resultVector; // int64_t: 64-bit values overflow int32
  std::string res;
  std::vector<int> compl;
  int lincombTerms = -1;

  std::string getTmpVname(int i) const;
  int64_t modRedInt(int64_t n) const;
  // x mod 2^bitCount (identity for bitCount >= 64, since values are already
  // 64-bit). Mirrors Python's `x % (2**bitCount)` without the UB of
  // `1LL << 64`.
  int64_t modInt(int64_t x) const;
  int getTermCount(const std::string &expr) const;
  int64_t prepareConstant(int64_t n) const;
  void collectAndEnumerateVariables();
  void initGroupSizes();
  void initResultVector();
  int computeBitwiseComplexityImpl(const std::string &expr) const;
  int computeAlternationLinear(const std::string &expr) const;
  int computeMetric(const std::string &e, Metric m, int t = -1) const;
  void checkSolutionComplexity(const std::string &e, int t = -1, int64_t constant = -1);
  int getTermCountOfCurrentSolution() const;
  std::string getBitwiseExpression(int offset = 0);
  std::string getBitwiseForVector(const std::vector<int64_t> &vector, int64_t offset = 0);
  std::string getNegatedBitwiseForVector(const std::vector<int64_t> &vector);
  bool isSumModulo(int64_t s1, int64_t s2, int64_t a) const;
  bool isDoubleModulo(int64_t a, int64_t b) const;
  std::string term(const std::string &bitwise, int64_t coeff, bool first);
  std::string compose(const std::vector<std::string> &bitwises,
                      const std::vector<int64_t> &coeffs);
  std::string termRefinement(int64_t r1, bool first, int64_t rAlt = -1);
  std::string expressionForEachUniqueValue(const std::vector<int64_t> &resultSet);
  void tryFindNegatedSingleExpression(const std::vector<int64_t> &resultSet);
  void tryEliminateUniqueValue(const std::vector<int64_t> &uniqueValues,
                               int64_t constant = -1);
  int64_t reduceByConstant();
  void findTwoExpressionsByTwoValues();
  std::vector<std::vector<Decision>> getDecisionVector(int64_t coeff1, int64_t coeff2,
                                                      const std::vector<int64_t> *vec = nullptr) const;
  bool mustSplit(const std::vector<std::vector<Decision>> &d) const;
  std::vector<std::vector<std::vector<Decision>>> split(
      std::vector<std::vector<Decision>> d);
  void determineCombOfTwoForCase(int64_t coeff1, int64_t coeff2,
                                 const std::vector<std::vector<Decision>> &caseVec,
                                 bool secNegated);
  void determineCombOfTwo(int64_t coeff1, int64_t coeff2,
                          const std::vector<int64_t> *vec = nullptr, bool secNegated = false);
  void tryFindNegatedAndUnnegatedExpression();
  void tryFindTwoNegatedExpressions();
  std::string addConstant(const std::string &expr, int64_t constant);
  void tryRefineSingleTerm(const std::vector<int64_t> &resultSet);
  void tryRefineTwoTermsFirstZero(const std::vector<int64_t> &resultSet);
  void tryRefineTwoTermsFirstNonZero(const std::vector<int64_t> &resultSet);
  void tryRefineTwoTerms(const std::vector<int64_t> &resultSet);
  bool checkTermCount(int value) const;
  void tryRefine();
  void simplifyOneValue(const std::vector<int64_t> &resultSet);
  std::vector<std::vector<int>> getVariableCombinations() const;
  std::string conjunction(int64_t coeff, const std::vector<int> &vars, bool first);
  bool areVariablesTrue(int n, const std::vector<int> &variables) const;
  void subtractCoefficient(int64_t coeff, int firstStart,
                           const std::vector<int> &variables);
  void simplifyGeneric();
  bool trySimplifyFewerVariables();
  std::vector<std::string> splitIntoTerms(const std::string &expr) const;
  std::vector<std::set<std::string>> findVariablesInTerms(
      const std::vector<std::string> &l) const;
  std::tuple<int, std::vector<int>, std::vector<int>, std::vector<int>, std::vector<int>>
  partitionTermsWrtVariableCount(const std::vector<std::string> &l,
                                 const std::vector<std::set<std::string>> &v) const;
  bool tryFindMatchingPartition(int i, const std::set<std::string> &variables,
                               std::vector<std::set<std::string>> &partitionV,
                               std::vector<std::vector<int>> &partitionT) const;
  std::pair<std::vector<int>, bool> determineIntersections(
      const std::set<std::string> &variables,
      const std::vector<std::set<std::string>> &partitionV) const;
  // NOTE: `lrem` is passed by reference (mirroring Python's list-by-reference
  // semantics): terms that cannot be partitioned are appended to it, and the
  // caller must use the updated list when composing the result.
  std::vector<std::vector<int>> partition(
      const std::vector<std::set<std::string>> &v, const std::vector<int> &l1,
      const std::vector<int> &l2, const std::vector<int> &l3,
      std::vector<int> &lrem);
  std::string composeTerms(const std::vector<std::string> &l,
                           const std::vector<int> &indices, bool leadingSign) const;
  bool isBitwiseWithBinop(const std::string &expr) const;
  std::string simplifyPartsAndCompose(
      const std::vector<std::string> &l, const std::vector<std::vector<int>> &partition,
      int constIdx, const std::vector<int> &lrem);
  void trySplit();
  bool verifyUsingZ3();
  int getVariableCount(const std::string &expr) const;
  std::string simplifyImpl(bool useZ3, bool alreadySplit = false);
  bool isInputLinear();
  bool checkVerify(const std::string &simpl);
};

// Simplify the given expression. Returns the simplified expression.
std::string simplifyLinearMba(const std::string &expr, int bitCount, bool useZ3,
                              bool checkLinearFlag = false, bool modRed = false,
                              bool refine = true, int verifBitCount = -1,
                              Metric metric = Metric::ALTERNATION);

} // namespace MBA
} // namespace LSiMBA
