#include "Simplifier.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"

#include <filesystem>
#include <fstream>
#include <future>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ShuttingYard.h"
#include "veque.h"

using namespace llvm;
using namespace std;
#include "MBAChecker.h"

namespace LSiMBA {
Simplifier::Simplifier(int bitCount, bool UseSigned, bool runParallel,
                       const std::string &expr)
    : bitCount(0), modulus(0), vnumber(0), SP64(expr.at(0)) {

  this->init(bitCount, UseSigned, runParallel, expr);
};

void Simplifier::init(int bitCount, bool UseSigned, bool runParallel,
                      const std::string &expr) {
  this->groupsizes = {1};
  this->bitCount = bitCount;
  this->modulus = pow(2, bitCount);
  this->originalVariables = {};
  this->originalExpression = expr;
  this->vnumber = 0;
  this->resultVector = {};

  this->RunParallel = runParallel;
  this->UseSigned = UseSigned;
  this->MaxThreadCount = thread::hardware_concurrency();

  this->parse_and_replace_variables();
  this->init_groupsizes();
  this->init_result_vector();
}

std::string &Simplifier::get_vname(int i) {
  if (this->varMap.find(i) != this->varMap.end())
    return this->varMap.at(i);
  else

  this->varMap[i] = "X[" + std::to_string(i) + "]";
  return this->varMap[i];
}

int64_t Simplifier::mod_red(int64_t n) {
  if (!n)
    return 0;

  if (this->UseSigned) {
    return n % this->modulus;
  } else {
    return (uint64_t)n % this->modulus;
  }
}

bool Simplifier::simplify(std::string &simp_exp, bool useZ3, bool fastCheck) {
  std::string simpl;

  if (this->vnumber > 3) {
    simpl = this->simplify_generic();
    this->try_simplify_fewer_variables(simpl);
  } else {
    std::set<int64_t> resultSet(this->resultVector.begin(),
                                this->resultVector.end());

    if (resultSet.size() == 1) {
      simpl = this->simplify_one_value(resultSet);
    } else {
      simpl = this->simplify_generic();
      this->try_refine(simpl);
    }
  }

  // Do fast verify
  bool Result = true;
  if (fastCheck) {
    if (!this->probably_equivalent(this->originalExpression, simpl)) {
      Result = false;
      // outs() << "[probably_equivalent] Simplification failed!\n";
    }
  }

  /*
    if (useZ3 && this->verify_mba_unsat(simpl)) {
      printf("Error in simplification! Simplified expression is not equivalent "
             "to original one!");
      return "";
    }
    */

  this->replace_variables_back(simpl);

  simp_exp = simpl;

  return Result;
}

bool Simplifier::probably_equivalent(std::string &expr0, std::string &expr1) {
  // Run in parallel if user wants and if there are more than 3 variables,
  // otherwise the performance is worse than in serial
  if (this->RunParallel && this->vnumber > 3) {
    return this->probably_equivalent_parallel(expr0, expr1);
  }

  auto f = [&](std::string &expr, llvm::SmallVector<int64_t, 16> &par) -> int64_t {
    auto n = eval(expr, par);
    auto m = this->mod_red(n);
    return m;
  };

  // if expr1 has no X[ then try to replace variables
  // happens when inner solver has replaced the variables back
  // Finally replace the variables by 'X<i>'.
  std::string expr1_replVar = expr1;
  for (int i = 0; i < this->vnumber; i++) {
    replace_all(expr1_replVar, this->originalVariables[i], this->get_vname(i));
  }

  llvm::SmallVector<int64_t, 16> par;
  for (int i = 0; i < NUM_TEST_CASES; i++) {
    par.clear();
    for (int j = 0; j < this->vnumber; j++) {
      par.push_back(SP64.next() % this->modulus);
    }

    // Compare results
    auto r0 = f(expr0, par);
    auto r1 = f(expr1_replVar, par);

    if (r0 != r1) {
      return false;
    }
  }

  return true;
}

bool Simplifier::probably_equivalent_parallel(std::string &expr0,
                                              std::string &expr1) {

  bool IsValid = true;

  auto f = [&](std::string &expr, llvm::SmallVector<int64_t, 16> &par) -> int64_t {
    auto n = eval(expr, par);
    auto m = this->mod_red(n);
    return m;
  };

  auto fcomp = [&](std::string expr0, std::string expr1) -> void {
    llvm::SmallVector<int64_t, 16> par;
    for (int j = 0; j < this->vnumber; j++) {
      par.push_back(SP64.next() % this->modulus);
    }

    auto r0 = f(expr0, par);
    auto r1 = f(expr1, par);
    if (r0 != r1) {
      IsValid = false;
    }
  };

  // if expr1 has no X[ then try to replace variables
  // happens when inner solver has replaced the variables back
  std::string expr1_replVar = expr1;
  for (int i = 0; i < this->vnumber; i++) {
    replace_all(expr1_replVar, this->originalVariables[i], this->get_vname(i));
  }

  int CurThreadCount = 0;
  veque::veque<thread> threads;

  for (int i = 0; i < NUM_TEST_CASES; i++) {
    threads.push_back(thread(fcomp, expr0, expr1_replVar));

    CurThreadCount++;

    // Wait for one thread to finish
    if (CurThreadCount == this->MaxThreadCount) {
      threads.front().join();
      threads.pop_front();

      --CurThreadCount;
    }

    if (!IsValid)
      break;
  }

  // Wait for threads to finish
  for (auto &t : threads) {
    if (t.joinable())
      t.join();
  }

  return IsValid;
}

bool Simplifier::is_double_modulo(int64_t a, int64_t b) {
  return ((2 * b) == a) || ((2 * b) == (a + this->modulus));
}

std::string
Simplifier::append_term_refinement(std::string &expr,
                                   std::vector<std::string> &bitwiseList,
                                   int64_t r1, bool IsrAlt, int64_t rAlt) {
  std::vector<int64_t> t;
  for (auto r2 : this->resultVector) {
    if (r2 == r1 || (rAlt && (r2 == rAlt))) {
      t.push_back(1);
    } else {
      t.push_back(0);
    }
  }

  auto index = this->get_bitwise_index_for_vector(t, 0);
  if (r1 == 1) {
    return expr + bitwiseList[index] + "+";
  }

  return expr + to_string(r1) + "*" + bitwiseList[index] + "+";
}

std::string Simplifier::expression_for_each_unique_value(
    std::set<int64_t> &resultSet, std::vector<std::string> &bitwiseList) {
  std::string expr = "";
  for (auto r : resultSet) {
    if (r != 0) {
      expr = this->append_term_refinement(expr, bitwiseList, r);
    }
  }

  // Get rid of '+' at the end.
  expr = expr.substr(0, expr.size() - 1);

  // If we only have 1 term, get rid of parentheses, if present.
  if (resultSet.size() == 1 && expr.front() == '(' && expr.back() == ')') {
    expr = expr.substr(1, expr.size() - 1);
  }

  return expr;
}

std::string Simplifier::try_find_negated_single_expression(
    std::set<int64_t> &resultSet, std::vector<std::string> &bitwiseList) {
  if (resultSet.size() != 2) {
    llvm::report_fatal_error("resultSet.size() != 2");
  }

  auto it = resultSet.begin();
  int64_t a = *it;
  ++it;
  int64_t b = *it;

  auto aDouble = this->is_double_modulo(a, b);
  auto bDouble = this->is_double_modulo(b, a);

  if (!aDouble && !bDouble) {
    return "";
  }

  // Make sure that b is double a.
  if (aDouble) {
    auto ta = a;
    a = b;
    b = ta;
  }

  if (this->resultVector[0] == b) {
    return "";
  }

  auto coeff = this->mod_red(-a);

  std::vector<int64_t> t;
  for (auto r : this->resultVector) {
    t.push_back(r == b);
  }

  auto index = this->get_bitwise_index_for_vector(t, 0);
  auto e = bitwiseList[index];
  if (e[0] == '~') {
    e = e.substr(1, e.size() - 1);
  } else {
    e = "~" + e;
  }

  if (coeff == 1) {
    return e;
  }

  return to_string(coeff) + "*" + e;
}

std::string
Simplifier::try_eliminate_unique_value(std::vector<int64_t> &uniqueValues,
                                       std::vector<std::string> &bitwiseList) {
  int l = uniqueValues.size();

  // NOTE: Would be possible also for higher l, implementation is generic.

  // Try to get rid of a value by representing it as a sum of the others.
  for (int i = 0; i < l - 1; i++) {
    for (int j = i + 1; j < l; j++) {
      for (int k = 0; k < l; k++) {
        if (k == i || k == j)
          continue;

        if (this->is_sum_modulo(uniqueValues[i], uniqueValues[j],
                                uniqueValues[k])) {
          std::string simpler = "";
          for (int i1 = i; i1 <= j; i1++) {
            simpler = this->append_term_refinement(
                simpler, bitwiseList, uniqueValues[i1], true, uniqueValues[k]);
          }

          if (l > 3) {
            std::set<int64_t> resultSet(uniqueValues.begin(),
                                        uniqueValues.end());
            resultSet.erase(uniqueValues[i]);
            resultSet.erase(uniqueValues[j]);
            resultSet.erase(uniqueValues[k]);

            while (resultSet.size() > 0) {
              auto r1 = *resultSet.begin();
              resultSet.erase(r1);
              simpler = this->append_term_refinement(simpler, bitwiseList, r1);
            }
          }

          simpler = simpler.substr(0, simpler.size() - 1);
          return simpler;
        }
      }
    }
  }

  if (l < 4) {
    return "";
  }

  // Finally, if we have more than 3 values, try to express one of them as
  // a sum of all others.
  auto sum = [](std::vector<int64_t> &uniqueValues) -> int64_t {
    int64_t sum = 0;
    for (auto v : uniqueValues) {
      sum += v;
    }
    return sum;
  };

  auto SumUniqueValues = sum(uniqueValues);
  for (int i = 0; i < l; i++) {
    if (!(2 * uniqueValues[i] == SumUniqueValues))
      continue;

    std::string simpler = "";
    for (int j = 0; j < l; j++) {
      if (i == j)
        continue;

      simpler = this->append_term_refinement(
          simpler, bitwiseList, uniqueValues[j], true, uniqueValues[i]);

      return simpler;
    }
  }

  return "";
}

void Simplifier::try_refine(std::string &expr) {
  this->init_result_vector();

  // The number of terms.
  int count = 1;
  for (int i = 0; i < expr.length(); i++) {
    if (expr[i] == '+') {
      count++;
    }
  }

  // The expression is already as simple as it can be.
  if (count <= 1) {
    return;
  }

  std::set<int64_t> resultSet(this->resultVector.begin(),
                              this->resultVector.end());

  int l = resultSet.size();
  if (l <= 1) {
    report_fatal_error("Expression is not simplified");
  }

  auto bitwiseList = this->get_bitwise_list();

  if (l == 2) {
    // (2) If only one nonzero value occurs and the result for all
    // variables being zero is zero, we can find a single expression.
    if (this->resultVector[0] == 0) {
      auto simpler =
          this->expression_for_each_unique_value(resultSet, bitwiseList);
      if (simpler.front() == '(' && simpler.back() == ')') {
        simpler = simpler.substr(1, simpler.size() - 2);
        if (simpler != "") {
          expr = simpler;
          return;
        }
      }
    }

    // (3) Check whether we can find one single negated term.
    auto simpler =
        this->try_find_negated_single_expression(resultSet, bitwiseList);
    if (simpler != "") {
      expr = simpler;
      return;
    }
  }

  // We cannot simplify the expression better.
  if (count <= 2)
    return;

  // If the first result is nonzero, this is the term's constant. Subtract it.
  auto constant = this->resultVector[0];
  if (constant != 0) {
    for (int i = 0; i < this->resultVector.size(); i++) {
      this->resultVector[i] -= constant;
      this->resultVector[i] = this->mod_red(this->resultVector[i]);
    }
  }

  resultSet.clear();
  resultSet.insert(this->resultVector.begin(), this->resultVector.end());

  l = resultSet.size();

  if (l == 2) {
    // (4) In this case we know that the constant is nonzero since we
    // would have run into the case above otherwise.
    expr = to_string(constant) + "+" +
           this->expression_for_each_unique_value(resultSet, bitwiseList);
    return;
  }

  if (l == 3 && constant == 0) {
    // (5) We can reduce the expression to two terms.
    expr = this->expression_for_each_unique_value(resultSet, bitwiseList);
    return;
  }

  std::vector<int64_t> uniqueValues;
  for (auto r : resultSet) {
    if (r != 0) {
      uniqueValues.push_back(r);
    }
  }

  if (l == 4 && constant == 0) {
    // (6) We can still come down to 2 expressions if we can express one
    // value as a sum of the others.
    auto simpler = this->try_eliminate_unique_value(uniqueValues, bitwiseList);
    if (simpler != "") {
      expr = simpler;
      return;
    }
  }

  // NOTE: One may additionally want to try to find a sum of two negated
  // expressions, or one negated and one unnegated...

  // We cannot simplify the expression better.
  if (count == 3) {
    return;
  }

  if (constant == 0) {
    // (7) Since the previous attempts failed, the best we can do is find
    // three expressions corresponding to the three unique values.
    expr = this->expression_for_each_unique_value(resultSet, bitwiseList);
    return;
  }

  // (8) Try to reduce the number of unique values by expressing one as a
  // combination of the others.
  auto simpler = this->try_eliminate_unique_value(uniqueValues, bitwiseList);
  if (simpler != "") {
    if (constant == 0) {
      expr = simpler;
      return;
    }
    expr = to_string(constant) + "+" + simpler;
    return;
  }  
}

void Simplifier::append_conjunction(std::string &expr, int64_t coeff,
                                           std::vector<int> &variables) {

  if (variables.size() <= 0) {
    report_fatal_error("No variables in append_conjunction");
  }

  if (coeff == 0) {
    return;
  }

  if (coeff != 1) {
    expr += std::to_string(coeff) + "*";
  }

  // If we have a nontrivial conjunction, we need parentheses.
  if (variables.size() > 1) {
    expr += "(";
  }

  for (int i = 0; i < variables.size(); i++) {
    expr += this->get_vname(variables[i]) + "&";
  }

  // Get rid of last '&'.
  expr = expr.substr(0, expr.size() - 1);

  if (variables.size() > 1) {
    expr += ")";
  }

  expr = expr + "+";
}

bool Simplifier::are_variables_true(int n, std::vector<int> &variables,
                                    int start) {
  int prev = 0;

  for (int i = start; i < variables.size(); i++) {
    int v = variables.at(i);
    n >>= (v - prev);
    prev = v;

    if ((n & 1) == 0) {
      return false;
    }
  }

  return true;
}

void Simplifier::subtract_coefficient(int64_t coeff, int firstStart,
                                      std::vector<int> &variables) {
  auto groupsize1 = this->groupsizes[variables[0]];
  auto period1 = 2 * groupsize1;

  for (int start = firstStart; start < this->resultVector.size();
       start += period1) {
    for (int i = start; i < (start + groupsize1); i++) {
      // The first variable is true by design of the for loops.
      if (i != firstStart && (variables.size() == 1 ||
                              this->are_variables_true(i, variables, 1))) {
        this->resultVector[i] -= coeff;
      }
    }
  }
}

std::string Simplifier::simplify_generic() {
  int l = this->resultVector.size();
  std::string expr = "";

  // The constant term.
  auto constant = this->mod_red(this->resultVector[0]);
  for (int i = 1; i < l; i++) {
    this->resultVector[i] -= constant;
  }

  if (constant) {
    expr += std::to_string(constant) + "+";
  }

  // # Append all conjunctions of variables (including trivial conjunctions
  // # of single variables) if their coefficients are nonzero
  std::vector<std::vector<int>> combinations;
  this->get_variable_combinations(combinations);

  for (auto &comb : combinations) {
    int index = 0;
    for (auto &v : comb) {
      index += this->groupsizes[v];
    }

    auto coeff = this->mod_red(this->resultVector[index]);

    if (coeff == 0)
      continue;

    this->subtract_coefficient(coeff, index, comb);

    this->append_conjunction(expr, coeff, comb);
  }

  if (expr == "") {
    return "0";
  }

  // Get rid of last '+'.
  expr = expr.substr(0, expr.size() - 1);

  // count '+' in expr
  int count = 0;
  for (int i = 0; i < expr.size(); i++) {
    if (expr[i] == '+') {
      count++;
    }
  }

  if (count == 0 && expr[0] == '(' && expr[expr.size() - 1] == ')') {
    expr = expr.substr(1, expr.size() - 2);
  }

  return expr;
}

void Simplifier::try_simplify_fewer_variables(std::string &expr) {
  std::vector<int64_t> occuring;

  for (int v = 0; v < this->vnumber; v++) {
    if (expr.find(this->get_vname(v)) != std::string::npos) {
      occuring.push_back(v);
    }
  }

  auto vnumber = occuring.size();

  // No function available for more than 3.
  if (vnumber > 3) {
    return;
  }

  // replace var names
  for (int i = 0; i < vnumber; i++) {
    std::string vname = this->get_vname(occuring[i]);
    size_t pos = 0;
    while ((pos = expr.find(vname, pos)) != std::string::npos) {
      auto var = string("v") + to_string(i);
      expr.replace(pos, vname.length(), var);
      pos += var.length();
    }
  }

  auto innerSimplifier =
      Simplifier(this->bitCount, this->UseSigned, this->RunParallel, expr);

  // Maybe not needed to set ""
  // expr = "";

  innerSimplifier.simplify(expr, false, false);

  for (int i = 0; i < vnumber; i++) {
    std::string var = string("v") + to_string(i);
    size_t pos = 0;
    while ((pos = expr.find(var, pos)) != std::string::npos) {
      expr.replace(pos, var.length(), this->originalVariables[occuring[i]]);
      pos += var.length();
    }
  }
}

std::string Simplifier::simplify_one_value(std::set<int64_t> &resultSet) {
  auto coefficient = *resultSet.begin();
  resultSet.erase(coefficient);

  auto simExpre = to_string(this->mod_red(coefficient));
  return simExpre;
}

void Simplifier::get_variable_combinations(std::vector<std::vector<int>> &comb) {
  for (int v = 0; v < this->vnumber; v++) {
    comb.push_back({v});
  }

  auto New = this->vnumber;

  for (int count = 1; count < this->vnumber; count++) {
    int size = comb.size();
    int nnew = 0;
    for (int e = size - New; e < size; e++) {
      for (int v = comb[e].back() + 1; v < this->vnumber; v++) {
        auto newComb = comb[e];
        newComb.push_back(v);
        comb.push_back(newComb);
        nnew++;
      }
      New = nnew;
    }
  }
}

std::vector<std::string> Simplifier::rex(std::string &expr,
                                         const std::string &regex) {
  std::vector<std::string> result;

  std::regex word_regex(regex);

  auto words_begin = std::sregex_iterator(expr.begin(), expr.end(), word_regex);
  auto words_end = std::sregex_iterator();

  for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
    std::smatch match = *i;
    std::string match_str = match.str();

    result.push_back(i->str());
  }

  return result;
}

void Simplifier::parse_and_replace_variables() {
  /*
   List of variables as well as binary or hexadecimal numbers (or
   potentially any illegal strings). We include these constants because
   we want to make sure not to consider parts of binary or hex numbers
   as variable names.
  */
  auto varsAndConstants =
      this->rex(this->originalExpression, VarAndConstsRegEx);

  std::set<std::string> varSet;
  for (auto &S : varsAndConstants) {
    if (S.front() != '0')
      varSet.insert(S);
  }

  // List of unique variables.
  for (auto &S : varSet) {
    this->originalVariables.push_back(S);
  }

  // # First sort in alphabetical order since it is nice to have, e.g., 'x'
  // preceding 'y'.
  std::sort(this->originalVariables.begin(), this->originalVariables.end());

  // Sort by length, but afterwards they have to be replaced in reverse
  // order in order not to replace substrings of variables.
  // std::sort(this->originalVariables.begin(), this->originalVariables.end());

  // List of binary or hex numbers.
  varSet.clear();
  for (auto &S : varsAndConstants) {
    if (S.front() == '0')
      varSet.insert(S);
  }

  // Again remove duplicates for efficiency.
  std::vector<std::string> constants;
  for (auto &S : varSet) {
    constants.push_back(S);
  }

  // # Sort the constants by length in order not to replace substrings of
  // larger numbers.
  std::sort(constants.begin(), constants.end());

  // Finally replace those numbers by their decimal equivalents.
  for (auto &c : constants) {
    if (c.size() <= 1)
      report_fatal_error("Constant is to small!");

    // convert to int
    int64_t n = std::atoll(c.c_str());
    string strN = to_string(n);

    // replace all occurences
    replace_all(this->originalExpression, c, strN);
  }

  this->vnumber = this->originalVariables.size();

  // Finally replace the variables by 'X<i>'.
  for (int i = 0; i < this->vnumber; i++) {
    replace_all(this->originalExpression, this->originalVariables[i],
                this->get_vname(i));
  }
}

void Simplifier::init_groupsizes() {
  for (int i = 1; i < this->vnumber; i++) {
    this->groupsizes.push_back(2 * this->groupsizes.back());
  }
}

/*
  # Initialize the vector storing results of expression evaluation for all
  # truth value combinations, i.e., [e(0,0,...), e(1,0,...), e(0,1,...),
  # e(1,1,...)].
*/
void Simplifier::init_result_vector() {
  // Run in parallel if user wants and if there are more than 3 variables,
  // otherwise the performance is worse than in serial
  if (this->RunParallel && this->vnumber > 3) {
    this->init_result_vector_parallel();
    return;
  }

  auto f = [&](llvm::SmallVector<int64_t, 16> &par) -> int64_t {
    auto n = eval(this->originalExpression, par);
    auto m = this->mod_red(n);
    return m;
  };

  this->resultVector.clear();
  for (int i = 0; i < pow(2, this->vnumber); i++) {
    int n = i;
    llvm::SmallVector<int64_t, 16> par;
    for (int j = 0; j < this->vnumber; j++) {
      par.push_back(n & 1);
      n = n >> 1;
    }

    this->resultVector.push_back(f(par));
  }
}

void Simplifier::init_result_vector_parallel() {
  auto f = [&](std::string expr, llvm::SmallVector<int64_t, 16> par) -> int64_t {
    auto n = eval(expr, par);
    auto m = this->mod_red(n);
    return m;
  };

  int CurThreadCount = 0;
  veque::veque<future<int64_t>> threads;

  this->resultVector.clear();
  for (int i = 0; i < pow(2, this->vnumber); i++) {
    llvm::SmallVector<int64_t, 16> par;

    int n = i;
    for (int j = 0; j < this->vnumber; j++) {
      par.push_back(n & 1);
      n = n >> 1;
    }

    threads.push_back(async(launch::async, f, this->originalExpression, par));
    CurThreadCount++;

    // Wait for first thread to finish, should be ok as they all take the same expression
    if (CurThreadCount == this->MaxThreadCount) {      
      this->resultVector.push_back(threads.front().get());
      threads.pop_front();

      --CurThreadCount;
    }
  }

  // Get remaining thread results
  for (auto &t : threads) {
    this->resultVector.push_back(t.get());
  }
}

void Simplifier::replace_variables_back(std::string &expr) {
  for (int i = 0; i < this->vnumber; i++) {
    std::string vname = this->get_vname(i);
    std::string var = this->originalVariables[i];
    size_t pos = 0;
    while ((pos = expr.find(vname, pos)) != std::string::npos) {
      expr.replace(pos, vname.length(), var);
      pos += var.length();
    }
  }
}

std::string Simplifier::strip(const std::string &inpt) {
  auto start_it = inpt.begin();
  auto end_it = inpt.rbegin();
  while (std::isspace(*start_it))
    ++start_it;
  while (std::isspace(*end_it))
    ++end_it;
  return std::string(start_it, end_it.base());
}

const std::vector<std::string> Simplifier::get_bitwise_list() {
  if (this->vnumber != 1 && this->vnumber != 2 && this->vnumber != 3) {
    report_fatal_error("Error: Only 1, 2 or 3 variables are supported!");
  }

  if (this->vnumber == 1) {
    return Bitwise_List_1;
  } else if (this->vnumber == 2) {
    return Bitwise_List_2;
  }

  // get current directory
  auto currentDir = filesystem::current_path();
  auto truthfile = currentDir / (string("bitwise_list_") +
                                 to_string(this->vnumber) + "vars.txt");

  // read lines from file
  std::ifstream infile(truthfile.c_str());
  if (infile.is_open() == false) {
    report_fatal_error("Could not open bitwise_list file!\n", false);
  }

  std::string line;
  std::vector<std::string> bitwiseExprList;

  while (std::getline(infile, line)) {
    // split line at '#' and strip whitespaces
    line = line.substr(0, line.find('#'));
    line = strip(line);

    bitwiseExprList.push_back(line);
  }

  return bitwiseExprList;
}

int Simplifier::get_bitwise_index_for_vector(std::vector<int64_t> &vector,
                                             int offset) {
  int index = 0;
  int add = 1;

  for (int i = 0; i < vector.size() - 1; i++) {
    if (vector[i + 1] != offset) {
      index += add;
    }
    add <<= 1;
  }

  return index;
}

int Simplifier::get_bitwise_index(int offset) {
  return this->get_bitwise_index_for_vector(this->resultVector, offset);
}

bool Simplifier::is_sum_modulo(int64_t s1, int64_t s2, int64_t a) {
  return ((s1 + s2) == a) || ((s1 + s2) == (a + this->modulus));
}

void Simplifier::replace_all(std::string &str, const std::string &from,
                             const std::string &to) {
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
}

bool Simplifier::simplify_linear_mba(bool UseSigned, std::string &expr,
                                     std::string &simp_expr, int bitCount,
                                     bool useZ3, bool checkLinear,
                                     bool fastCheck, bool runParallel) {
  MBAChecker C;
  if (checkLinear && !C.check_expression(expr)) {
    printf("Error: Input expression may be no linear MBA: %s\n", expr.c_str());
    return "";
  }

  Simplifier S(bitCount, UseSigned, runParallel, expr);
  auto R = S.simplify(simp_expr, useZ3, fastCheck);
  return R;
}

} // namespace LSiMBA