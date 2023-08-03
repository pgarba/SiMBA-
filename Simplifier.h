#ifndef SIMPLIFIER_H
#define SIMPLIFIER_H

#include <map>
#include <set>
#include <string>
#include <vector>

#include <cstdint>

#include <llvm/ADT/APInt.h>

#include "splitmix64.h"

// LSimba namespace
namespace LSiMBA {

const std::string VarAndConstsRegEx = "[0]?[a-zA-Z][a-zA-Z0-9_]*";

class Simplifier {
public:
  Simplifier(int bitCount, bool runParallel, const std::string &expr);

  Simplifier(int bitCount, bool runParallel, int VNumber,
             std::vector<llvm::APInt> ResultVector);

  static bool simplify_linear_mba(std::string &expr, std::string &simp_expr,
                                  int bitCount = 64, bool useZ3 = false,
                                  bool checkLinear = false,
                                  bool fastCheck = true,
                                  bool runParallel = true);

  // Some helpers
  static std::string strip(const std::string &inpt);

  static void replaceAllStrings(std::string &str, const std::string &from,
                                const std::string &to);

  static std::vector<std::string> getVariables(std::string &expr);

  bool simplify(std::string &simp_exp, bool useZ3, bool fastCheck);

  bool external_simplifier(llvm::StringRef expr, std::string &simp_exp,
                           bool useZ3, bool fastCheck,
                           const llvm::StringRef ExternalSimplifierPath,
                           int BitWidth, bool Debug);

private:
  bool RunParallel;

  int MaxThreadCount;

  std::vector<int64_t> groupsizes;

  int bitCount;

  llvm::APInt modulus;

  std::vector<std::string> originalVariables;

  std::string originalExpression;

  int vnumber;

  std::vector<llvm::APInt> resultVector;

  std::vector<llvm::APInt> initialResultVector;

  SplitMix64 SP64;

  void init(int bitCount, bool runParallel, const std::string &expr);

  std::map<int64_t, std::string> varMap;
  std::string &get_vname(int i);

  void fillResultSet(std::vector<llvm::APInt> &resultSet,
                     std::vector<llvm::APInt> &inputVector);

  llvm::APInt mod_red(const llvm::APInt &n, bool Signed = false);

  void parse_and_replace_variables();

  void init_groupsizes();

  void set_initial_result_vector();

  void init_result_vector();

  void init_result_vector_parallel();

  void replace_variables_back(std::string &expr);

  const std::vector<std::string> &get_bitwise_list();

  int get_bitwise_index_for_vector(std::vector<llvm::APInt> &vector,
                                   int offset);

  int get_bitwise_index(int offset);

  bool is_sum_modulo(const llvm::APInt &s1, const llvm::APInt &s2,
                     const llvm::APInt &a);

  bool is_double_modulo(llvm::APInt &a, llvm::APInt &b);

  // replacing string here leads to a bug
  std::string append_term_refinement(
      std::string &expr, const std::vector<std::string> &bitwiseList,
      const llvm::APInt &r1, bool IsrAlt, const llvm::APInt &rAlt);

  std::string
  expression_for_each_unique_value(std::vector<llvm::APInt> &resultSet,
                                   const std::vector<std::string> &bitwiseList);

  std::string try_find_negated_single_expression(
      std::vector<llvm::APInt> &resultSet,
      const std::vector<std::string> &bitwiseList);

  std::string
  try_eliminate_unique_value(std::vector<llvm::APInt> &uniqueValues,
                             const std::vector<std::string> &bitwiseList);

  void try_refine(std::string &expr);

  void append_conjunction(std::string &expr, const llvm::APInt &coeff,
                          std::vector<int> &variables);

  bool are_variables_true(int n, std::vector<int> &variables, int start = 0);

  void subtract_coefficient(const llvm::APInt &coeff, int firstStart,
                            std::vector<int> &variables);

  std::string simplify_generic();

  void try_simplify_fewer_variables(std::string &expr);

  std::string simplify_one_value(std::vector<llvm::APInt> &resultSet);

  void get_variable_combinations(std::vector<std::vector<int>> &comb);

  static std::vector<std::string> rex(std::string &expr,
                                      const std::string &regex);

  void replace_all(std::string &str, const std::string &from,
                   const std::string &to);

  bool probably_equivalent(std::string &expr0, std::string &expr1);
  bool probably_equivalent_parallel(std::string &expr0, std::string &expr1);

  bool verify_mba_unsat(std::string &expr0, std::string &expr1);
};
} // namespace LSiMBA

#endif // SIMPLIFIER_H