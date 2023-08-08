#include "Simplifier.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <math.h>
#include <memory>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "BitwiseList.h"
#include "CSiMBA.h"
#include "MBAChecker.h"
#include "Modulo.h"
#include "ShuttingYard.h"
#include "Z3Prover.h"
#include "veque.h"

using namespace llvm;
using namespace std;

cl::opt<std::string> PythonPath("python-path", cl::Optional,
                                cl::desc("Path to python binary"),
                                cl::value_desc("python-path"),
                                cl::init("python"));

cl::opt<bool> RedisCache("enable-redis-cache", cl::Optional,
                         cl::desc("Enable redis cache in GAMBA"),
                         cl::value_desc("redis-cache"), cl::init(false));

cl::opt<bool> EnableSHR("enable-shr", cl::Optional,
                        cl::desc("Enable SHR in external solving in GAMBA"),
                        cl::value_desc("enable-shr"), cl::init(true));

cl::opt<bool>
    EnableMod("enable-mod-red", cl::Optional,
              cl::desc("Enable modulo reduction of constants in GAMBA"),
              cl::value_desc("enable-mod-red"), cl::init(false));

static bool WarnOnce = false;

namespace LSiMBA {
Simplifier::Simplifier(int bitCount, bool runParallel, const std::string &expr)
    : bitCount(0), vnumber(0), SP64(expr.at(0)) {
  this->init(bitCount, runParallel, expr);
};

Simplifier::Simplifier(int bitCount, bool runParallel, int VNumber,
                       std::vector<llvm::APInt> ResultVector)
    : bitCount(0), vnumber(0), SP64(VNumber * bitCount) {
  this->groupsizes = {1};
  this->bitCount = bitCount;
  this->modulus = getModulus(bitCount); // pow(2, bitCount);
  this->originalVariables = {};
  this->originalExpression = "";
  this->vnumber = VNumber;

  // fill result vector
  for (const APInt &v : ResultVector) {
    this->resultVector.push_back(v);
  }

  this->RunParallel = runParallel;
  this->MaxThreadCount = thread::hardware_concurrency();

  // create fake variables
  for (int i = 0; i < this->vnumber; i++) {
    char c = 'a' + i;
    string strC = string(1, c);
    this->originalVariables.push_back(strC);
  }

  // Store initial result vector for later use
  this->initialResultVector = this->resultVector;

  this->init_groupsizes();
}

void Simplifier::init(int bitCount, bool runParallel, const std::string &expr) {
  this->groupsizes = {1};
  this->bitCount = bitCount;
  this->modulus = getModulus(bitCount);
  this->originalVariables = {};
  this->originalExpression = expr;
  this->vnumber = 0;
  this->resultVector = {};

  this->RunParallel = runParallel;
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

void Simplifier::fillResultSet(std::vector<llvm::APInt> &resultSet,
                               std::vector<llvm::APInt> &inputVector) {
  for (auto &v : inputVector) {
    // Check if v is in resultSet
    bool Found = false;
    for (auto &vs : resultSet) {
      if (v.zextOrTrunc(vs.getBitWidth()).eq(vs)) {
        // if (vs.eq(v)) {
        Found = true;
        break;
      }
    }

    if (Found)
      continue;

    resultSet.push_back(v);
  }
}

llvm::APInt Simplifier::mod_red(const llvm::APInt &n, bool Signed) {
  int OldBitWidth = n.getBitWidth();

  if (Signed) {
    // return n.srem(this->modulus);
    return n.sextOrTrunc(this->modulus.getBitWidth())
        .srem(this->modulus)
        .trunc(OldBitWidth);
  } else {
    // return n.urem(this->modulus);
    return n.sextOrTrunc(this->modulus.getBitWidth())
        .urem(this->modulus)
        .trunc(OldBitWidth);
  }
}

int exec(std::string &cmd, std::string &output) {
#ifdef _WIN32
  // if cmd to long popen will crash
  if (cmd.size() > 62000)
    return 1;

  std::shared_ptr<FILE> pipe(_popen(cmd.c_str(), "r"), _pclose);
#else
  std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
#endif
  if (!pipe)
    return 1;

  char buffer[10000];
  while (!feof(pipe.get())) {
    if (fgets(buffer, 10000, pipe.get()) != NULL) {

      std::string temp = buffer;

      // Check for "*** ... simplified to"
      const std::string SimplifiedTo = "*** ... simplified to ";
      if (!strncmp(temp.c_str(), SimplifiedTo.c_str(), SimplifiedTo.size())) {
        output = temp.substr(SimplifiedTo.size());
        output.erase(std::remove(output.begin(), output.end(), '\n'),
                     output.cend());

        return 0;
      }

      // Check for Error:
      const std::string Error = "Error: ";
      if (!strncmp(temp.c_str(), Error.c_str(), Error.size())) {
        errs() << "[!] External simplifier failed: " << temp << "\n";
        return 1;
      }
    }
  }

  // Something went wrong
  return 1;
}

bool Simplifier::external_simplifier(
    llvm::StringRef expr, std::string &simp_exp, bool useZ3, bool fastCheck,
    const llvm::StringRef ExternalSimplifierPath, int BitWidth, bool Debug) {

  // >> is not supported
  if (!EnableSHR && expr.find(">>") != std::string::npos) {
    if (Debug && !WarnOnce) {
      outs() << "[!] '>>' operator not supported!\n";
      WarnOnce = true;
    }
    return false;
  }

  // Replace '>>' with '>'
  std::string exprStr = expr.str();
  Simplifier::replaceAllStrings(exprStr, ">>", ">");
  expr = exprStr;

  // Check if file exists with llvm
  if (!sys::fs::exists(ExternalSimplifierPath)) {
    errs() << "[!] External simplifier file does not exist: "
           << ExternalSimplifierPath << "\n";
    return false;
  }

  // Execute external simplifier
  std::string output = "";
  std::string cmd = PythonPath + " " + ExternalSimplifierPath.str() + " \"" +
                    expr.str() + "\"" + " -b " + to_string(BitWidth);

  if (RedisCache) {
    cmd += " -r";
  }

  if (EnableMod) {
    cmd += " -m";
  }

  auto exitCode = exec(cmd, output);

  if (exitCode != 0) {
    return false;
  }

  // Check if output is valid
  if (output.empty()) {
    // Might be a timeout or other error
    return false;
  }

  simp_exp = output;

  return true;
}

bool Simplifier::simplify(std::string &simp_exp, bool useZ3, bool fastCheck) {
  std::string simpl;

  if (this->vnumber > 3) {
    simpl = this->simplify_generic();
    this->try_simplify_fewer_variables(simpl);
  } else {
    std::vector<APInt> resultSet;
    fillResultSet(resultSet, this->resultVector);

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
    }
  }

  // Replace variables back
  this->replace_variables_back(simpl);
  this->replace_variables_back(originalExpression);

  if (Result && useZ3 &&
      this->verify_mba_unsat(simpl, this->originalExpression)) {
    printf("[Error] Simplified expression is not equivalent "
           "to original one!");
    Result = false;
  }

  simp_exp = simpl;

  return Result;
}

bool Simplifier::verify_mba_unsat(std::string &expr0, std::string &expr1) {
  return !proveReplacement(expr0, expr1, this->bitCount,
                           this->originalVariables);
}

bool Simplifier::probably_equivalent(std::string &expr0, std::string &expr1) {
  // Run in parallel if user wants and if there are more than 3 variables,
  // otherwise the performance is worse than in serial
  if (this->RunParallel && this->vnumber > 3) {
    return this->probably_equivalent_parallel(expr0, expr1);
  }

  auto f = [&](std::string &expr,
               llvm::SmallVector<APInt, 16> &par) -> uint64_t {
    auto n = eval(expr, par, bitCount);
    auto OldBitWidth = n.getBitWidth();
    if (n.isSignBitSet()) {
      // return n.srem(this->modulus).getZExtValue();
      return n.sextOrTrunc(this->modulus.getBitWidth())
          .srem(this->modulus)
          .trunc(OldBitWidth)
          .getZExtValue();
    } else {
      // return n.urem(this->modulus).getZExtValue();
      return n.sextOrTrunc(this->modulus.getBitWidth())
          .urem(this->modulus)
          .trunc(OldBitWidth)
          .getZExtValue();
    }
  };

  // if expr1 has no X[ then try to replace variables
  // happens when inner solver has replaced the variables back
  // Finally replace the variables by 'X<i>'.
  std::string expr1_replVar = expr1;
  for (int i = 0; i < this->vnumber; i++) {
    replace_all(expr1_replVar, this->originalVariables[i], this->get_vname(i));
  }

  llvm::SmallVector<APInt, 16> par;
  for (int i = 0; i < NUM_TEST_CASES; i++) {
    par.clear();
    for (int j = 0; j < this->vnumber; j++) {
      // par.push_back(APInt(bitCount, SP64.next()).urem(this->modulus));
      par.push_back(APInt(this->modulus.getBitWidth(), SP64.next())
                        .urem(this->modulus)
                        .zextOrTrunc(bitCount));
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

  auto f = [&](std::string &expr,
               llvm::SmallVector<APInt, 16> &par) -> uint64_t {
    auto n = eval(expr, par, bitCount);
    auto OldBitWidth = n.getBitWidth();
    if (n.isSignBitSet()) {
      // return n.srem(this->modulus).getZExtValue();
      return n.sextOrTrunc(this->modulus.getBitWidth())
          .srem(this->modulus)
          .trunc(OldBitWidth)
          .getZExtValue();
    } else {
      // return n.urem(this->modulus).getZExtValue();
      return n.sextOrTrunc(this->modulus.getBitWidth())
          .urem(this->modulus)
          .trunc(OldBitWidth)
          .getZExtValue();
    }
  };

  auto fcomp = [&](std::string expr0, std::string expr1) -> void {
    llvm::SmallVector<APInt, 16> par;
    for (int j = 0; j < this->vnumber; j++) {
      // par.push_back(APInt(bitCount, SP64.next()).urem(this->modulus));
      par.push_back(APInt(this->modulus.getBitWidth(), SP64.next())
                        .urem(this->modulus)
                        .zextOrTrunc(bitCount));
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

bool Simplifier::is_double_modulo(llvm::APInt &a, llvm::APInt &b) {
  // return ((2 * b) == a) || ((2 * b) == (a + this->modulus));

  if ((2 * b) == a)
    return true;

  auto A = a.sextOrTrunc(this->modulus.getBitWidth());
  auto B = b.sextOrTrunc(this->modulus.getBitWidth());

  if ((2 * A) == (B + this->modulus))
    return true;

  return false;
}

std::string Simplifier::append_term_refinement(
    std::string &expr, const std::vector<std::string> &bitwiseList,
    const APInt &r1, bool IsrAlt, const APInt &rAlt) {
  std::vector<APInt> t;
  for (auto &r2 : this->resultVector) {
    auto r2_ex = r2.sextOrTrunc(r1.getBitWidth());
    if (r2_ex == r1 || (IsrAlt && (r2_ex == rAlt))) {
      t.push_back(APInt(bitCount, 1));
    } else {
      t.push_back(APInt(bitCount, 0));
    }
  }

  auto index = this->get_bitwise_index_for_vector(t, 0);
  if (r1 == 1) {
    return expr + bitwiseList[index] + "+";
  }

  return expr + to_string(r1.getZExtValue()) + "*" + bitwiseList[index] + "+";
}

std::string Simplifier::expression_for_each_unique_value(
    std::vector<APInt> &resultSet,
    const std::vector<std::string> &bitwiseList) {
  std::string expr = "";
  for (auto &r : resultSet) {
    if (r.isZero() == false) {
      expr = this->append_term_refinement(expr, bitwiseList, r, false,
                                          APInt(bitCount, 0));
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
    std::vector<APInt> &resultSet,
    const std::vector<std::string> &bitwiseList) {
  if (resultSet.size() != 2) {
    llvm::report_fatal_error("resultSet.size() != 2");
  }

  auto a = resultSet[0];
  auto b = resultSet[1];

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

  auto coeff = this->mod_red(-a, true);

  std::vector<APInt> t;
  for (auto &r : this->resultVector) {
    t.push_back(APInt(bitCount, r == b));
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

  return to_string(coeff.getZExtValue()) + "*" + e;
}

std::string Simplifier::try_eliminate_unique_value(
    std::vector<llvm::APInt> &uniqueValues,
    const std::vector<std::string> &bitwiseList) {
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
            std::vector<llvm::APInt>
                resultSet; //(uniqueValues.begin(), uniqueValues.end());
            fillResultSet(resultSet, uniqueValues);

            // resultSet.erase(uniqueValues[i]);
            auto It = std::remove(resultSet.begin(), resultSet.end(),
                                  uniqueValues[i]);
            resultSet.erase(It);

            // resultSet.erase(uniqueValues[j]);
            It = std::remove(resultSet.begin(), resultSet.end(),
                             uniqueValues[j]);
            resultSet.erase(It);

            // resultSet.erase(uniqueValues[k]);
            It = std::remove(resultSet.begin(), resultSet.end(),
                             uniqueValues[k]);
            resultSet.erase(It);

            while (resultSet.size() > 0) {
              auto r1 = *resultSet.begin();
              It = std::remove(resultSet.begin(), resultSet.end(), r1);
              resultSet.erase(It);

              simpler = this->append_term_refinement(simpler, bitwiseList, r1,
                                                     false, APInt(bitCount, 0));
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
  auto sum = [](std::vector<APInt> &uniqueValues) -> APInt {
    llvm::APInt Sum(128, 0);
    for (auto &v : uniqueValues) {
      Sum += v.getZExtValue();
    }
    return Sum;
  };

  auto double128 = [](APInt &Value) -> APInt {
    llvm::APInt V128(128, Value.getZExtValue());
    V128 += V128;
    return V128;
  };

  auto SumUniqueValues = sum(uniqueValues);
  for (int i = 0; i < l; i++) {
    if (!(double128(uniqueValues[i]) == SumUniqueValues))
      continue;

    std::string simpler = "";
    for (int j = 0; j < l; j++) {
      if (i == j)
        continue;

      simpler = this->append_term_refinement(
          simpler, bitwiseList, uniqueValues[j], true, uniqueValues[i]);

      // Get rid of '+' at the end.
      simpler = simpler.substr(0, simpler.size() - 1);

      return simpler;
    }
  }

  return "";
}

void Simplifier::try_refine(std::string &expr) {
  // Use inital result vector.
  this->set_initial_result_vector();

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

  std::vector<APInt> resultSet;
  fillResultSet(resultSet, this->resultVector);

  int l = resultSet.size();
  if (l <= 1) {
    report_fatal_error("Expression is not simplified");
  }

  auto &bitwiseList = this->get_bitwise_list();

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
      this->resultVector[i] = this->mod_red(
          this->resultVector[i], this->resultVector[i].isSignBitSet());
    }
  }

  resultSet.clear();
  fillResultSet(resultSet, this->resultVector);

  l = resultSet.size();

  if (l == 2) {
    // (4) In this case we know that the constant is nonzero since we
    // would have run into the case above otherwise.
    expr = to_string(constant.getZExtValue()) + "+" +
           this->expression_for_each_unique_value(resultSet, bitwiseList);
    return;
  }

  if (l == 3 && constant == 0) {
    // (5) We can reduce the expression to two terms.
    expr = this->expression_for_each_unique_value(resultSet, bitwiseList);
    return;
  }

  std::vector<APInt> uniqueValues;
  for (auto &r : resultSet) {
    if (r != 0) {
      uniqueValues.push_back(r);
    }
  }
  std::sort(uniqueValues.begin(), uniqueValues.end(),
            [](APInt &L, APInt &R) { return !R.ule(L); });

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
    expr = to_string(constant.getZExtValue()) + "+" + simpler;
    return;
  }
}

void Simplifier::append_conjunction(std::string &expr, const llvm::APInt &coeff,
                                    std::vector<int> &variables) {
  if (variables.size() <= 0) {
    report_fatal_error("No variables in append_conjunction");
  }

  if (coeff == 0) {
    return;
  }

  if (coeff != 1) {
    expr += std::to_string(coeff.getZExtValue()) + "*";
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

void Simplifier::subtract_coefficient(const llvm::APInt &coeff, int firstStart,
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
  auto constant = this->mod_red(this->resultVector[0], true);
  for (int i = 1; i < l; i++) {
    this->resultVector[i] -=
        constant.sextOrTrunc(this->resultVector[i].getBitWidth());
  }

  if (!constant.isZero()) {
    expr += std::to_string(constant.getZExtValue()) + "+";
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

    auto coeff = this->mod_red(this->resultVector[index], true);

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

  auto innerSimplifier = Simplifier(this->bitCount, this->RunParallel, expr);

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

std::string
Simplifier::simplify_one_value(std::vector<llvm::APInt> &resultSet) {
  auto coefficient = *resultSet.begin();

  auto It = std::remove(resultSet.begin(), resultSet.end(), coefficient);
  resultSet.erase(It);

  auto simExpre = to_string(
      this->mod_red(coefficient, coefficient.isSignBitSet()).getZExtValue());
  return simExpre;
}

void Simplifier::get_variable_combinations(
    std::vector<std::vector<int>> &comb) {
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

std::vector<std::string> Simplifier::getVariables(std::string &expr) {
  std::vector<std::string> Variables;

  auto varsAndConstants = rex(expr, VarAndConstsRegEx);

  std::set<std::string> varSet;
  for (auto &S : varsAndConstants) {
    if (S.front() != '0')
      varSet.insert(S);
  }

  // List of unique variables.
  for (auto &S : varSet) {
    Variables.push_back(S);
  }

  // # First sort in alphabetical order since it is nice to have, e.g., 'x'
  // preceding 'y'.
  std::sort(Variables.begin(), Variables.end());

  return Variables;
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
  std::sort(constants.begin(), constants.end(), std::greater<>());

  // Finally replace those numbers by their decimal equivalents.
  for (auto &c : constants) {
    if (c.size() <= 1)
      report_fatal_error("Constant is to small!");

    // convert to decimal string
    int64_t n = 0;
    auto valid = llvm::to_integer(c, n);
    if (!valid) {
      report_fatal_error("Could not parse integer!");
    }

    // create a string
    string strN = llvm::utostr(n);

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

void Simplifier::set_initial_result_vector() {
  this->resultVector = this->initialResultVector;
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

  auto f = [&](llvm::SmallVector<APInt, 16> &par) -> APInt {
    auto n = eval(this->originalExpression, par, bitCount);
    auto OldBitWidth = n.getBitWidth();
    if (n.isSignBitSet()) {
      // return n.srem(this->modulus);
      return n.sextOrTrunc(this->modulus.getBitWidth())
          .srem(this->modulus)
          .trunc(OldBitWidth);
    } else {
      // return n.urem(this->modulus);
      return n.sextOrTrunc(this->modulus.getBitWidth())
          .urem(this->modulus)
          .trunc(OldBitWidth);
    }
  };

  this->resultVector.clear();
  for (int i = 0; i < pow(2, this->vnumber); i++) {
    int n = i;
    llvm::SmallVector<APInt, 16> par;
    for (int j = 0; j < this->vnumber; j++) {
      par.push_back(APInt(bitCount, n & 1));
      n = n >> 1;
    }

    this->resultVector.push_back(f(par));
  }

  // Store initial result vector for later use
  this->initialResultVector = this->resultVector;
}

void Simplifier::init_result_vector_parallel() {
  auto f = [&](std::string expr, llvm::SmallVector<APInt, 16> par) -> APInt {
    auto n = eval(expr, par, bitCount);
    auto OldBitWidth = n.getBitWidth();
    if (n.isSignBitSet()) {
      // return n.srem(this->modulus);
      return n.sextOrTrunc(this->modulus.getBitWidth())
          .srem(this->modulus)
          .trunc(OldBitWidth);
    } else {
      // return n.urem(this->modulus);
      return n.sextOrTrunc(this->modulus.getBitWidth())
          .urem(this->modulus)
          .trunc(OldBitWidth);
    }
  };

  int CurThreadCount = 0;
  veque::veque<future<APInt>> threads;

  this->resultVector.clear();
  for (int i = 0; i < pow(2, this->vnumber); i++) {
    llvm::SmallVector<APInt, 16> par;

    int n = i;
    for (int j = 0; j < this->vnumber; j++) {
      par.push_back(APInt(bitCount, n & 1));
      n = n >> 1;
    }

    threads.push_back(async(launch::async, f, this->originalExpression, par));
    CurThreadCount++;

    // Wait for first thread to finish, should be ok as they all take the same
    // expression
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

void Simplifier::replaceAllStrings(std::string &str, const std::string &from,
                                   const std::string &to) {
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
}

const std::vector<std::string> &Simplifier::get_bitwise_list() {
  if (this->vnumber != 1 && this->vnumber != 2 && this->vnumber != 3) {
    report_fatal_error("[!] Error: Only 1, 2 or 3 variables are supported!");
  }

  if (this->vnumber == 1) {
    return Bitwise_List_1;
  } else if (this->vnumber == 2) {
    return Bitwise_List_2;
  } else if (this->vnumber == 3) {
    return Bitwise_List_3;
  }

  // unreachable
  report_fatal_error("");
}

int Simplifier::get_bitwise_index_for_vector(std::vector<llvm::APInt> &vector,
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

bool Simplifier::is_sum_modulo(const llvm::APInt &s1, const llvm::APInt &s2,
                               const llvm::APInt &a) {
  // return ((s1 + s2) == a) || ((s1 + s2) == (a + this->modulus));
  if ((s1 + s2) == a)
    return true;

  auto S1 = s1.sextOrTrunc(this->modulus.getBitWidth());
  auto S2 = s2.sextOrTrunc(this->modulus.getBitWidth());
  auto A = a.sextOrTrunc(this->modulus.getBitWidth());

  if ((S1 + S2) == (A + this->modulus))
    return true;

  return false;
}

void Simplifier::replace_all(std::string &str, const std::string &from,
                             const std::string &to) {
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
}

bool Simplifier::simplify_linear_mba(std::string &expr, std::string &simp_expr,
                                     int bitCount, bool useZ3, bool checkLinear,
                                     bool fastCheck, bool runParallel) {
  MBAChecker C;
  if (checkLinear && !C.check_expression(expr)) {
    printf("Error: Input expression may be no linear MBA: %s\n", expr.c_str());
    return "";
  }

  Simplifier S(bitCount, runParallel, expr);
  auto R = S.simplify(simp_expr, useZ3, fastCheck);

  return R;
}

} // namespace LSiMBA