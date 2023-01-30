#ifndef SIMPLIFIER_H
#define SIMPLIFIER_H

#include <map>
#include <set>
#include <string>
#include <vector>

#include <cstdint>

#include "splitmix64.h"

// LSimba namespace
namespace LSiMBA {

// Some helper
std::string strip(const std::string &inpt);

class Simplifier {
public:
  Simplifier(int bitCount, bool UseSigned, bool runParallel,
             const std::string &expr);

  static bool simplify_linear_mba(bool UseSigned, std::string &expr,
                                  std::string &simp_expr, int bitCount = 64,
                                  bool useZ3 = false, bool checkLinear = false,
                                  bool fastCheck = true,
                                  bool runParallel = true);

  // Some helpers
  static std::string strip(const std::string &inpt);

private:
  const std::string VarAndConstsRegEx = "[0]?[a-zA-Z][a-zA-Z0-9_]*";

  const std::vector<std::string> Bitwise_List_1 = {"0", "X[0]"};

  const std::vector<std::string> Bitwise_List_2 = {
      "0",             // [0 0 0 0]
      "(X[0]&~X[1])",  // [0 1 0 0]
      "~(X[0]|~X[1])", // [0 0 1 0]
      "(X[0]^X[1])",   // [0 1 1 0]
      "(X[0]&X[1])",   // [0 0 0 1]
      "X[0]",          // [0 1 0 1]
      "X[1]",          // [0 0 1 1]
      "(X[0]|X[1])"    // [0 1 1 1]
  };

  bool RunParallel;

  bool UseSigned;

  int MaxThreadCount;

  std::vector<int64_t> groupsizes;

  int bitCount;

  int64_t modulus;

  std::vector<std::string> originalVariables;

  std::string originalExpression;

  int vnumber;

  std::vector<int64_t> resultVector;

  void init(int bitCount, bool UseSigned, bool runParallel,
            const std::string &expr);

  std::map<int64_t, std::string> varMap;
  std::string &get_vname(int i);

  int64_t mod_red(int64_t n);

  void parse_and_replace_variables();

  void init_groupsizes();

  void init_result_vector();

  void init_result_vector_parallel();

  void replace_variables_back(std::string &expr);

  const std::vector<std::string> get_bitwise_list();

  int get_bitwise_index_for_vector(std::vector<int64_t> &vector, int offset);

  int get_bitwise_index(int offset);

  bool is_sum_modulo(int64_t s1, int64_t s2, int64_t a);

  bool is_double_modulo(int64_t a, int64_t b);

  // replacing string here leads to a bug
  std::string append_term_refinement(std::string &expr,
                                     std::vector<std::string> &bitwiseList,
                                     int64_t r1, bool IsrAlt = false,
                                     int64_t rAlt = 0);

  std::string
  expression_for_each_unique_value(std::set<int64_t> &resultSet,
                                   std::vector<std::string> &bitwiseList);

  std::string
  try_find_negated_single_expression(std::set<int64_t> &resultSet,
                                     std::vector<std::string> &bitwiseList);

  std::string try_eliminate_unique_value(std::vector<int64_t> &uniqueValues,
                                         std::vector<std::string> &bitwiseList);

  void try_refine(std::string &expr);

  void append_conjunction(std::string &expr, int64_t coeff,
                          std::vector<int> &variables);

  bool are_variables_true(int n, std::vector<int> &variables, int start = 0);

  void subtract_coefficient(int64_t coeff, int firstStart,
                            std::vector<int> &variables);

  std::string simplify_generic();

  void try_simplify_fewer_variables(std::string &expr);

  std::string simplify_one_value(std::set<int64_t> &resultSet);

  void get_variable_combinations(std::vector<std::vector<int>> &comb);

  std::vector<std::string> rex(std::string &expr, const std::string &regex);

  void replace_all(std::string &str, const std::string &from,
                   const std::string &to);

  bool simplify(std::string &simp_exp, bool useZ3, bool fastCheck);

  // heuristic to check if MBA is equal
  const int NUM_TEST_CASES = 256;

  SplitMix64 SP64;

  bool probably_equivalent(std::string &expr0, std::string &expr1);
  bool probably_equivalent_parallel(std::string &expr0, std::string &expr1);
};
} // namespace LSiMBA

#endif // SIMPLIFIER_H