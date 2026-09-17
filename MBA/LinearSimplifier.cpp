// GAMBA native C++ port — linear MBA simplifier implementation.
#include "LinearSimplifier.h"

#include <algorithm>
#include <set>

#include "Parser.h"

namespace LSiMBA {
namespace MBA {

bool checkLinear(const std::string &expr, int bitCount) {
  auto tree = parse(expr, bitCount, false, false, false);
  if (tree == nullptr)
    return false;
  tree->refine();
  tree->markLinear();
  return tree->isLinear();
}

int countTerms(const std::string &expr) {
  int c = 0;
  for (char ch : expr)
    if (ch == '+' || ch == '-')
      c++;
  return c + (expr.empty() || expr[0] != '-' ? 1 : 0);
}

int computeBitwiseComplexity(const Node &root) {
  std::vector<NodeType> types = {NodeType::VARIABLE, NodeType::NEGATION};
  return root.countNodes(&types);
}

LinearSimplifier::LinearSimplifier(int bitCount, const std::string &expr, bool modRed,
                                   bool refine, int verifBitCount, Metric metric)
    : bitCount(bitCount),
      modulus(bitCount >= 64 ? ~0LL : (1LL << bitCount)),
      modRed(modRed), refine(refine),
      verifBitCount(verifBitCount), metric(metric) {
  origExpr = expr;
  tree = parse(expr, bitCount, modRed, false, false);
  valid = (tree != nullptr);

  if (valid) {
    collectAndEnumerateVariables();
    initGroupSizes();
    initResultVector();
  }
}

std::string LinearSimplifier::getTmpVname(int i) const {
  return "X[" + std::to_string(i) + "]";
}

int64_t LinearSimplifier::modRedInt(int64_t n) const {
  if (bitCount >= 64)
    return n;
  int64_t r = n % modulus;
  if (r < 0)
    r += modulus;
  return r;
}

int64_t LinearSimplifier::modInt(int64_t x) const {
  if (bitCount >= 64)
    return x;
  return x % modulus;
}

int LinearSimplifier::getTermCount(const std::string &expr) const {
  int c = 0;
  for (char ch : expr)
    if (ch == '+' || ch == '-')
      c++;
  return c - (expr[0] == '-' ? 1 : 0) + 1;
}

int64_t LinearSimplifier::prepareConstant(int64_t n) const {
  n = modRedInt(n);
  if (modRed)
    return n;
  if (bitCount >= 64)
    return n;
  if (n > modulus / 2)
    return n - modulus;
  return n;
}

void LinearSimplifier::collectAndEnumerateVariables() {
  variables.clear();
  tree->collectAndEnumerateVariables(variables);
  vnumber = static_cast<int>(variables.size());
  bitwiseFactory = std::make_shared<BitwiseFactory>(vnumber, &variables);
}

void LinearSimplifier::initGroupSizes() {
  for (int i = 1; i < vnumber; ++i)
    groupSizes.push_back(2 * groupSizes.back());
}

void LinearSimplifier::initResultVector() {
  resultVector.clear();
  for (int i = 0; i < (1 << vnumber); ++i) {
    int n = i;
    std::vector<uint64_t> par;
    for (int j = 0; j < vnumber; ++j) {
      par.push_back(n & 1);
      n >>= 1;
    }
    resultVector.push_back(static_cast<int64_t>(tree->eval(par)));
  }
}

int LinearSimplifier::computeBitwiseComplexityImpl(const std::string &expr) const {
  auto node = parse(expr, bitCount, modRed, false, false);
  if (node == nullptr)
    return 0;
  return computeBitwiseComplexity(*node);
}

int LinearSimplifier::computeAlternationLinear(const std::string &expr) const {
  auto node = parse(expr, bitCount, modRed, false, false);
  if (node == nullptr)
    return 0;
  return node->computeAlternationLinear();
}

int LinearSimplifier::computeMetric(const std::string &e, Metric m, int t) const {
  if (m == Metric::ALTERNATION)
    return computeAlternationLinear(e);
  if (m == Metric::TERMS)
    return t != -1 ? t : countTerms(e);
  if (m == Metric::STRING)
    return static_cast<int>(e.size());
  return computeBitwiseComplexityImpl(e);
}

void LinearSimplifier::checkSolutionComplexity(const std::string &e, int t,
                                              int64_t constant) {
  std::string expr = e;
  if (constant != -1) {
    expr = addConstant(e, constant);
    if (t != -1)
      t += 1;
  }

  if (res.empty()) {
    res = expr;
    complVec.assign(static_cast<int>(Metric::COUNT) - static_cast<int>(metric), -1);
    if (t != -1 && metric <= Metric::TERMS)
      complVec[static_cast<int>(Metric::TERMS) - static_cast<int>(metric)] = t;
    return;
  }

  std::vector<int> newCompl(static_cast<int>(Metric::COUNT) - static_cast<int>(metric),
                            -1);
  for (int m = 0; m < static_cast<int>(Metric::COUNT) - static_cast<int>(metric); ++m) {
    Metric mm = static_cast<Metric>(static_cast<int>(metric) + m);
    newCompl[m] = computeMetric(expr, mm, t);
    if (complVec[m] == -1)
      complVec[m] = computeMetric(res, mm);

    if (newCompl[m] > complVec[m])
      return;
    if (newCompl[m] < complVec[m]) {
      res = expr;
      complVec = newCompl;
      return;
    }
  }
}

int LinearSimplifier::getTermCountOfCurrentSolution() const {
  int m = static_cast<int>(Metric::TERMS) - static_cast<int>(metric);
  return complVec[m];
}

std::string LinearSimplifier::getBitwiseExpression(int offset) {
  return bitwiseFactory->createBitwise(resultVector, false, offset);
}

std::string LinearSimplifier::getBitwiseForVector(const std::vector<int64_t> &vector, int64_t offset) {
  return bitwiseFactory->createBitwise(vector, false, offset);
}

std::string LinearSimplifier::getNegatedBitwiseForVector(const std::vector<int64_t> &vector) {
  return bitwiseFactory->createBitwise(vector, true);
}

bool LinearSimplifier::isSumModulo(int64_t s1, int64_t s2, int64_t a) const {
  if (bitCount >= 64)
    return s1 + s2 == a;
  return s1 + s2 == a || s1 + s2 == a + modulus;
}

bool LinearSimplifier::isDoubleModulo(int64_t a, int64_t b) const {
  if (bitCount >= 64)
    return 2 * b == a;
  return 2 * b == a || 2 * b == a + modulus;
}

std::string LinearSimplifier::term(const std::string &bitwise, int64_t coeff, bool first) {
  int64_t c = prepareConstant(coeff);
  std::string termStr;

  if (!first || c < 0) {
    termStr += c >= 0 ? "+" : "-";
    if (c < 0)
      c = -c;
  }

  if (c != 1)
    termStr += std::to_string(c) + "*";
  termStr += bitwise;

  return termStr;
}

std::string LinearSimplifier::compose(const std::vector<std::string> &bitwises,
                                     const std::vector<int64_t> &coeffs) {
  std::string simpl;
  for (size_t i = 0; i < bitwises.size(); ++i)
    simpl += term(bitwises[i], coeffs[i], i == 0);
  return simpl;
}

std::string LinearSimplifier::termRefinement(int64_t r1, bool first, int64_t rAlt) {
  std::vector<int64_t> t;
  for (int64_t r2 : resultVector)
    t.push_back(r2 == r1 || (rAlt != -1 && r2 == rAlt) ? 1 : 0);

  std::string bitwise = getBitwiseForVector(t);
  return term(bitwise, r1, first);
}

std::string LinearSimplifier::expressionForEachUniqueValue(
    const std::vector<int64_t> &resultSet) {
  std::string expr;
  bool first = true;
  for (int64_t r : resultSet) {
    if (r != 0) {
      expr += termRefinement(r, first);
      first = false;
    }
  }

  if (resultSet.size() == 1 && !expr.empty() && expr[0] == '(' &&
      expr.back() == ')')
    expr = expr.substr(1, expr.size() - 2);

  return expr;
}

void LinearSimplifier::tryFindNegatedSingleExpression(
    const std::vector<int64_t> &resultSet) {
  std::vector<int64_t> s = resultSet;
  int64_t a = s.back();
  s.pop_back();
  int64_t b = s.back();
  s.pop_back();

  bool aDouble = isDoubleModulo(a, b);
  bool bDouble = isDoubleModulo(b, a);
  if (!aDouble && !bDouble)
    return;

  if (aDouble)
    std::swap(a, b);

  if (resultVector[0] == b)
    return;

  std::vector<int64_t> t;
  for (int64_t r : resultVector)
    t.push_back(r == b ? 1 : 0);
  std::string e = getNegatedBitwiseForVector(t);

  e = term(e, -a, true);
  if (!e.empty() && e[0] == '(' && e.back() == ')')
    e = e.substr(1, e.size() - 2);

  checkSolutionComplexity(e, 1);
}

void LinearSimplifier::tryEliminateUniqueValue(const std::vector<int64_t> &uniqueValues,
                                              int64_t constant) {
  int l = static_cast<int>(uniqueValues.size());
  if (l > 4)
    return;

  for (int i = 0; i < l - 1; ++i) {
    for (int j = i + 1; j < l; ++j) {
      for (int k = 0; k < l; ++k) {
        if (k == i || k == j)
          continue;

        if (isSumModulo(uniqueValues[i], uniqueValues[j], uniqueValues[k])) {
          std::string simpler;
          for (int i1 : {i, j})
            simpler += termRefinement(uniqueValues[i1], i1 == i, uniqueValues[k]);

          if (l > 3) {
            std::set<int64_t> resultSet(uniqueValues.begin(), uniqueValues.end());
            resultSet.erase(uniqueValues[i]);
            resultSet.erase(uniqueValues[j]);
            resultSet.erase(uniqueValues[k]);

            while (!resultSet.empty()) {
              int64_t r1 = *resultSet.begin();
              resultSet.erase(resultSet.begin());
              simpler += termRefinement(r1, false);
            }
          }

          checkSolutionComplexity(simpler, l - 1, constant);
          return;
        }
      }
    }
  }

  if (l < 4)
    return;

  int64_t sum = 0;
  for (int64_t v : uniqueValues)
    sum += v;
  for (int i = 0; i < l; ++i) {
    if (2 * uniqueValues[i] != sum)
      continue;

    std::string simpler;
    bool first = true;
    for (int j = 0; j < l; ++j) {
      if (i == j)
        continue;
      simpler += termRefinement(uniqueValues[j], first, uniqueValues[i]);
      first = false;
    }

    checkSolutionComplexity(simpler, l - 1, constant);
    return;
  }
}

int64_t LinearSimplifier::reduceByConstant() {
  int64_t constant = resultVector[0];
  if (constant != 0) {
    for (size_t i = 0; i < resultVector.size(); ++i) {
      resultVector[i] -= constant;
      resultVector[i] = static_cast<int64_t>(modRedInt(resultVector[i]));
    }
  }
  return constant;
}

void LinearSimplifier::findTwoExpressionsByTwoValues() {
  std::set<int64_t> resultSet(resultVector.begin(), resultVector.end());
  resultSet.erase(0);

  int64_t a = *resultSet.begin();
  resultSet.erase(resultSet.begin());
  int64_t b = *resultSet.begin();

  determineCombOfTwo(a, b);
  determineCombOfTwo(modRedInt(a - b), b);
  determineCombOfTwo(a, modRedInt(b - a));
}

std::vector<std::vector<Decision>> LinearSimplifier::getDecisionVector(
    int64_t coeff1, int64_t coeff2, const std::vector<int64_t> *vec) const {
  const std::vector<int64_t> &v = (vec != nullptr) ? *vec : resultVector;
  std::vector<std::vector<Decision>> d;

  for (int64_t r : v) {
    std::vector<Decision> e;
    bool f = modInt(r - coeff1) == 0;
    bool s = modInt(r - coeff2) == 0;
    bool b = modInt(r - coeff1 - coeff2) == 0;
    if (r == 0 && vnumber > 4)
      b = false;
    if (f && s && vnumber > 4)
      s = false;

    if (modInt(r) == 0)
      e.push_back(Decision::NONE);
    if (b)
      e.push_back(Decision::BOTH);
    if (f)
      e.push_back(Decision::FIRST);
    if (s)
      e.push_back(Decision::SECOND);

    d.push_back(e);
  }

  return d;
}

bool LinearSimplifier::mustSplit(const std::vector<std::vector<Decision>> &d) const {
  for (const auto &e : d)
    if (e.size() > 1)
      return true;
  return false;
}

std::vector<std::vector<std::vector<Decision>>> LinearSimplifier::split(
    std::vector<std::vector<Decision>> d) {
  std::vector<std::vector<Decision>> sec;
  bool splitFlag = false;

  for (auto &e : d) {
    if (splitFlag) {
      sec.push_back(e);
      continue;
    }
    if (e.size() > 1) {
      splitFlag = true;
      sec.push_back({e.back()});
      e.pop_back();
      continue;
    }
    sec.push_back(e);
  }

  return {d, sec};
}

void LinearSimplifier::determineCombOfTwoForCase(
    int64_t coeff1, int64_t coeff2, const std::vector<std::vector<Decision>> &caseVec,
    bool secNegated) {
  std::vector<int64_t> l1;
  for (const auto &c : caseVec)
    l1.push_back(c == std::vector<Decision>{Decision::FIRST} ||
                       c == std::vector<Decision>{Decision::BOTH}
                     ? 1
                     : 0);
  std::string first = getBitwiseForVector(l1);

  std::vector<int64_t> l2;
  for (const auto &c : caseVec)
    l2.push_back(c == std::vector<Decision>{Decision::SECOND} ||
                       c == std::vector<Decision>{Decision::BOTH}
                     ? 1
                     : 0);
  std::string second =
      secNegated ? getNegatedBitwiseForVector(l2) : getBitwiseForVector(l2);

  std::string e = compose({first, second},
                          {coeff1, secNegated ? -coeff2 : coeff2});
  checkSolutionComplexity(e, 2);
}

void LinearSimplifier::determineCombOfTwo(int64_t coeff1, int64_t coeff2,
                                          const std::vector<int64_t> *vec, bool secNegated) {
  auto d = getDecisionVector(coeff1, coeff2, vec);
  std::vector<std::vector<std::vector<Decision>>> cases = {d};

  while (!cases.empty()) {
    auto caseVec = cases.back();
    cases.pop_back();
    if (mustSplit(caseVec)) {
      auto splitRes = split(caseVec);
      for (auto &s : splitRes)
        cases.push_back(s);
      continue;
    }

    determineCombOfTwoForCase(coeff1, coeff2, caseVec, secNegated);
  }
}

void LinearSimplifier::tryFindNegatedAndUnnegatedExpression() {
  std::set<int64_t> unique(resultVector.begin(), resultVector.end());
  if (unique.size() != 3 && unique.size() != 4)
    return;

  int64_t negCoeff = resultVector[0];
  std::vector<int64_t> vec;
  for (int64_t a : resultVector)
    vec.push_back(static_cast<int64_t>(modRedInt(a - negCoeff)));

  std::vector<int64_t> uniqueValues;
  for (int64_t r : std::set<int64_t>(vec.begin(), vec.end()))
    if (r != 0 && r != negCoeff)
      uniqueValues.push_back(r);

  if (uniqueValues.size() > 2)
    return;

  if (uniqueValues.size() == 2) {
    int64_t a = uniqueValues[0];
    int64_t b = uniqueValues[1];

    if (modInt(b - a - negCoeff) != 0) {
      std::swap(a, b);
      if (modInt(b - a - negCoeff) != 0)
        return;
    }

    int64_t unnegCoeff = a;
    determineCombOfTwo(unnegCoeff, negCoeff, &vec, true);
    return;
  }

  int64_t a = uniqueValues[0];
  determineCombOfTwo(a, negCoeff, &vec, true);
  determineCombOfTwo(modRedInt(a - negCoeff), negCoeff, &vec, true);
}

void LinearSimplifier::tryFindTwoNegatedExpressions() {
  std::set<int64_t> unique(resultVector.begin(), resultVector.end());
  if (unique.size() != 3 && unique.size() != 4)
    return;

  int64_t coeffSum = resultVector[0];
  std::vector<int64_t> vec;
  for (int64_t a : resultVector)
    vec.push_back(static_cast<int64_t>(modRedInt(a - coeffSum)));

  std::vector<int64_t> uniqueValues;
  for (int64_t r : std::set<int64_t>(vec.begin(), vec.end()))
    if (r != 0 && r != coeffSum)
      uniqueValues.push_back(r);

  if (uniqueValues.size() > 2)
    return;
  if (uniqueValues.size() == 1)
    return;

  int64_t a = uniqueValues[0];
  int64_t b = uniqueValues[1];

  if (modInt(b + a - coeffSum) != 0)
    return;

  int64_t coeff1 = a;
  std::vector<int64_t> l;
  for (int64_t r : vec)
    l.push_back(r == coeff1 || r == coeffSum ? 1 : 0);
  std::string bitwise1 = getNegatedBitwiseForVector(l);

  int64_t coeff2 = b;
  for (size_t i = 0; i < vec.size(); ++i)
    vec[i] = static_cast<int64_t>(modRedInt(vec[i] - coeff1 * l[i]));
  std::vector<int64_t> vec2;
  for (int64_t r : vec)
    vec2.push_back(r == coeff2 ? 1 : 0);
  std::string bitwise2 = getNegatedBitwiseForVector(vec2);

  std::string e = compose({bitwise1, bitwise2}, {-coeff1, -coeff2});
  checkSolutionComplexity(e, 2);
}

std::string LinearSimplifier::addConstant(const std::string &expr, int64_t constant) {
  if (constant == 0)
    return expr;

  std::string e = expr;
  if (isBitwiseWithBinop(e))
    e = "(" + e + ")";

  int64_t c = prepareConstant(constant);
  std::string prefix = std::to_string(c);
  if (!e.empty() && e[0] != '-')
    prefix += "+";

  return prefix + e;
}

void LinearSimplifier::tryRefineSingleTerm(const std::vector<int64_t> &resultSet) {
  int l = static_cast<int>(resultSet.size());
  if (l > 2)
    return;

  if (resultVector[0] == 0) {
    std::string e = expressionForEachUniqueValue(resultSet);
    if (!e.empty() && e[0] == '(' && e.back() == ')')
      e = e.substr(1, e.size() - 2);
    checkSolutionComplexity(e, 1);
  }

  tryFindNegatedSingleExpression(resultSet);
}

void LinearSimplifier::tryRefineTwoTermsFirstZero(const std::vector<int64_t> &resultSet) {
  int l = static_cast<int>(resultSet.size());

  if (l == 3) {
    findTwoExpressionsByTwoValues();
  } else if (l == 4) {
    std::vector<int64_t> uniqueValues;
    for (int64_t r : std::set<int64_t>(resultVector.begin(), resultVector.end()))
      if (r != 0)
        uniqueValues.push_back(r);
    tryEliminateUniqueValue(uniqueValues);
  }
}

void LinearSimplifier::tryRefineTwoTermsFirstNonZero(
    const std::vector<int64_t> &resultSet) {
  int l = static_cast<int>(resultSet.size());

  std::vector<int64_t> resultVectorCopy = resultVector;

  if (l == 2) {
    int64_t constant = reduceByConstant();
    std::string e = expressionForEachUniqueValue(
        std::vector<int64_t>(resultVector.begin(), resultVector.end()));
    e = addConstant(e, constant);
    checkSolutionComplexity(e, 2);

    resultVector = resultVectorCopy;
  }

  if (l <= 4) {
    tryFindNegatedAndUnnegatedExpression();
    tryFindTwoNegatedExpressions();
  }
}

void LinearSimplifier::tryRefineTwoTerms(const std::vector<int64_t> &resultSet) {
  if (resultVector[0] == 0)
    tryRefineTwoTermsFirstZero(resultSet);
  else
    tryRefineTwoTermsFirstNonZero(resultSet);
}

bool LinearSimplifier::checkTermCount(int value) const {
  if (lincombTerms <= value)
    return true;
  if (metric != Metric::TERMS)
    return false;
  return getTermCountOfCurrentSolution() <= value;
}

void LinearSimplifier::tryRefine() {
  initResultVector();

  if (checkTermCount(1))
    return;

  std::set<int64_t> resultSet(resultVector.begin(), resultVector.end());
  std::vector<int64_t> resultSetVec(resultSet.begin(), resultSet.end());

  tryRefineSingleTerm(resultSetVec);

  if (checkTermCount(2))
    return;

  tryRefineTwoTerms(resultSetVec);

  if (checkTermCount(3))
    return;

  int64_t constant = reduceByConstant();
  std::vector<int64_t> uniqueValues;
  for (int64_t r : std::set<int64_t>(resultVector.begin(), resultVector.end()))
    if (r != 0)
      uniqueValues.push_back(r);
  tryEliminateUniqueValue(uniqueValues, constant);

  int c = static_cast<int>(uniqueValues.size()) + (constant != 0 ? 1 : 0);
  if (checkTermCount(c))
    return;

  std::string simpler = expressionForEachUniqueValue(uniqueValues);
  simpler = addConstant(simpler, constant);
  checkSolutionComplexity(simpler, c);
}

void LinearSimplifier::simplifyOneValue(const std::vector<int64_t> &resultSet) {
  std::set<int64_t> s(resultSet.begin(), resultSet.end());
  int64_t coefficient = *s.begin();
  std::string e = std::to_string(prepareConstant(coefficient));
  checkSolutionComplexity(e, 1);
}

std::vector<std::vector<int>> LinearSimplifier::getVariableCombinations() const {
  std::vector<std::vector<int>> comb;
  for (int v = 0; v < vnumber; ++v)
    comb.push_back({v});
  int newCount = vnumber;

  for (int count = 1; count < vnumber; ++count) {
    int size = static_cast<int>(comb.size());
    int nnew = 0;
    for (int i = size - newCount; i < size; ++i) {
      for (int v = comb[i].back() + 1; v < vnumber; ++v) {
        auto e = comb[i];
        e.push_back(v);
        comb.push_back(e);
        nnew++;
      }
    }
    newCount = nnew;
  }

  return comb;
}

std::string LinearSimplifier::conjunction(int64_t coeff, const std::vector<int> &vars,
                                          bool first) {
  if (coeff == 0)
    return "";

  std::string conj;
  if (vars.size() > 1)
    conj += "(";

  for (int v : vars)
    conj += variables[v] + "&";

  conj = conj.substr(0, conj.size() - 1);
  if (vars.size() > 1)
    conj += ")";
  return term(conj, coeff, first);
}

bool LinearSimplifier::areVariablesTrue(int n, const std::vector<int> &variables) const {
  int prev = 0;
  for (int v : variables) {
    n >>= (v - prev);
    prev = v;
    if ((n & 1) == 0)
      return false;
  }
  return true;
}

void LinearSimplifier::subtractCoefficient(int64_t coeff, int firstStart,
                                          const std::vector<int> &variables) {
  int groupsize1 = groupSizes[variables[0]];
  int period1 = 2 * groupsize1;
  for (int start = firstStart; start < static_cast<int>(resultVector.size());
       start += period1) {
    for (int i = start; i < start + groupsize1; ++i) {
      if (i != firstStart &&
          (variables.size() == 1 || areVariablesTrue(i, {variables.begin() + 1,
                                                         variables.end()}))) {
        resultVector[i] -= coeff;
      }
    }
  }
}

void LinearSimplifier::simplifyGeneric() {
  int l = static_cast<int>(resultVector.size());
  std::string expr;
  int termCount = 0;

  int64_t constant = prepareConstant(resultVector[0]);
  for (int i = 1; i < l; ++i)
    resultVector[i] -= constant;

  bool first = true;
  if (constant != 0) {
    expr += std::to_string(constant);
    termCount += 1;
    first = false;
  }

  auto combinations = getVariableCombinations();
  for (const auto &comb : combinations) {
    int index = 0;
    for (int v : comb)
      index += groupSizes[v];
    int64_t coeff = prepareConstant(resultVector[index]);

    if (coeff == 0)
      continue;

    subtractCoefficient(coeff, index, comb);
    expr += conjunction(coeff, comb, first);
    termCount += 1;
    first = false;
  }

  if (expr.empty())
    expr = "0";
  else if (expr[0] == '(' && expr.back() == ')' && termCount == 1)
    expr = expr.substr(1, expr.size() - 2);

  checkSolutionComplexity(expr, termCount);
  lincombTerms = termCount;
}

bool LinearSimplifier::trySimplifyFewerVariables() {
  std::vector<std::string> occuring;
  auto t = parse(res, bitCount, modRed, false, false);
  if (t == nullptr)
    return false;
  t->collectAndEnumerateVariables(occuring);

  int vnumber = static_cast<int>(occuring.size());
  if (vnumber > 3)
    return false;

  std::vector<std::string> tmpVars;
  for (int i = 0; i < vnumber; ++i)
    tmpVars.push_back(getTmpVname(i));
  std::string expr = t->toString(false, -1, &tmpVars);

  LinearSimplifier innerSimplifier(bitCount, expr, modRed, refine, verifBitCount, metric);
  expr = innerSimplifier.simplifyImpl(false);

  t = parse(expr, bitCount, modRed, false, false);
  t->enumerateVariables(tmpVars);
  expr = t->toString(false, -1, &occuring);

  checkSolutionComplexity(expr);
  return true;
}

std::string LinearSimplifier::simplifyImpl(bool useZ3, bool alreadySplit) {
  if (vnumber > 3) {
    if (alreadySplit) {
      std::set<int64_t> resultSet(resultVector.begin(), resultVector.end());
      if (refine && resultSet.size() == 1)
        simplifyOneValue(std::vector<int64_t>(resultSet.begin(), resultSet.end()));
      else {
        simplifyGeneric();
        if (refine)
          tryRefine();
      }
    } else {
      simplifyGeneric();
      if (!trySimplifyFewerVariables())
        trySplit();
    }
  } else {
    std::set<int64_t> resultSet(resultVector.begin(), resultVector.end());
    if (refine && resultSet.size() == 1)
      simplifyOneValue(std::vector<int64_t>(resultSet.begin(), resultSet.end()));
    else {
      simplifyGeneric();
      if (refine)
        tryRefine();
    }
  }

  return res;
}

bool LinearSimplifier::isInputLinear() {
  tree->refine();
  tree->markLinear();
  return tree->isLinear();
}

bool LinearSimplifier::checkVerify(const std::string &simpl) {
  if (verifBitCount == -1)
    return true;
  return isInputLinear();
}

std::string LinearSimplifier::simplify(bool useZ3) {
  res.clear();
  complVec.clear();

  std::string simpl = simplifyImpl(useZ3);
  return checkVerify(simpl) ? simpl : "";
}

// ---- helpers used by trySplit / simplifyPartsAndCompose ----

std::vector<std::string> LinearSimplifier::splitIntoTerms(const std::string &expr) const {
  std::vector<std::string> l;
  std::string cur;
  for (char ch : expr) {
    if (ch == '+' || ch == '-') {
      if (!cur.empty()) {
        l.push_back(cur);
        cur.clear();
      }
      l.push_back(std::string(1, ch));
    } else {
      cur += ch;
    }
  }
  if (!cur.empty())
    l.push_back(cur);

  if (!l.empty() && l[0] == "+")
    l.erase(l.begin());
  if (l.empty() || l[0] != "-")
    l.insert(l.begin(), "+");

  return l;
}

std::vector<std::set<std::string>> LinearSimplifier::findVariablesInTerms(
    const std::vector<std::string> &l) const {
  std::vector<std::set<std::string>> v;
  for (const auto &e : l) {
    v.push_back({});
    if (e == "+" || e == "-")
      continue;

    auto node = parse(e, bitCount, modRed, false, false);
    if (node != nullptr) {
      std::vector<std::string> vars;
      node->collectAndEnumerateVariables(vars);
      v.back() = std::set<std::string>(vars.begin(), vars.end());
    }
  }
  return v;
}

std::tuple<int, std::vector<int>, std::vector<int>, std::vector<int>, std::vector<int>>
LinearSimplifier::partitionTermsWrtVariableCount(
    const std::vector<std::string> &l,
    const std::vector<std::set<std::string>> &v) const {
  int constIdx = -1;
  std::vector<int> l1, l2, l3, lrem;

  for (size_t i = 0; i < v.size(); ++i) {
    int lv = static_cast<int>(v[i].size());
    if (lv == 0) {
      if (l[i] != "+" && l[i] != "-")
        constIdx = static_cast<int>(i);
      continue;
    } else if (lv == 1) {
      l1.push_back(static_cast<int>(i));
    } else if (lv == 2) {
      l2.push_back(static_cast<int>(i));
    } else if (lv == 3) {
      l3.push_back(static_cast<int>(i));
    } else {
      lrem.push_back(static_cast<int>(i));
    }
  }

  return {constIdx, l1, l2, l3, lrem};
}

bool LinearSimplifier::tryFindMatchingPartition(
    int i, const std::set<std::string> &variables,
    std::vector<std::set<std::string>> &partitionV,
    std::vector<std::vector<int>> &partitionT) const {
  for (size_t j = 0; j < partitionV.size(); ++j) {
    std::set<std::string> diff = variables;
    for (const auto &x : partitionV[j])
      diff.erase(x);
    if (diff.empty()) {
      partitionT[j].push_back(i);
      return true;
    }
  }
  return false;
}

std::pair<std::vector<int>, bool> LinearSimplifier::determineIntersections(
    const std::set<std::string> &variables,
    const std::vector<std::set<std::string>> &partitionV) const {
  std::vector<int> intersections;
  bool valid = true;

  for (size_t j = 0; j < partitionV.size(); ++j) {
    const auto &part = partitionV[j];

    std::set<std::string> inter;
    for (const auto &x : variables)
      if (part.count(x))
        inter.insert(x);
    if (inter.empty())
      continue;

    intersections.push_back(static_cast<int>(j));

    if (valid) {
      std::set<std::string> uni = variables;
      for (const auto &x : part)
        uni.insert(x);
      if (intersections.size() > 0 || uni.size() > 3)
        valid = false;
    }
  }

  return {intersections, valid};
}

std::vector<std::vector<int>> LinearSimplifier::partition(
    const std::vector<std::set<std::string>> &v, const std::vector<int> &l1,
    const std::vector<int> &l2, const std::vector<int> &l3,
    std::vector<int> &lrem) {
  std::vector<std::set<std::string>> partitionV;
  std::vector<std::vector<int>> partitionT;
  std::set<std::string> remV;

  for (int i : lrem)
    for (const auto &x : v[i])
      remV.insert(x);

  std::vector<int> l23 = l2;
  for (int x : l3)
    l23.push_back(x);

  for (int i : l23) {
    std::set<std::string> inter;
    for (const auto &x : v[i])
      if (remV.count(x))
        inter.insert(x);
    if (!inter.empty()) {
      for (const auto &x : v[i])
        remV.insert(x);
      lrem.push_back(i);
      continue;
    }

    if (tryFindMatchingPartition(i, v[i], partitionV, partitionT))
      continue;

    auto interRes = determineIntersections(v[i], partitionV);
    const auto &intersections = interRes.first;

    if (intersections.empty()) {
      partitionV.push_back(v[i]);
      partitionT.push_back({i});
    } else if (intersections.size() == 1) {
      partitionV[intersections[0]] =
          partitionV[intersections[0]];
      for (const auto &x : v[i])
        partitionV[intersections[0]].insert(x);
      partitionT[intersections[0]].push_back(i);
    } else {
      for (const auto &x : v[i])
        remV.insert(x);
      lrem.push_back(i);

      for (auto it = intersections.rbegin(); it != intersections.rend(); ++it) {
        int j = *it;
        for (const auto &x : partitionV[j])
          remV.insert(x);
        for (int x : partitionT[j])
          lrem.push_back(x);

        partitionV.erase(partitionV.begin() + j);
        partitionT.erase(partitionT.begin() + j);
      }
    }
  }

  for (int i : l1) {
    std::set<std::string> vi = v[i];
    std::string var = *vi.begin();

    if (remV.count(var)) {
      lrem.push_back(i);
      continue;
    }

    bool done = false;
    for (size_t j = 0; j < partitionV.size(); ++j) {
      if (partitionV[j].count(var)) {
        partitionT[j].push_back(i);
        done = true;
        break;
      }
    }

    if (!done) {
      partitionV.push_back({var});
      partitionT.push_back({i});
    }
  }

  return partitionT;
}

std::string LinearSimplifier::composeTerms(const std::vector<std::string> &l,
                                          const std::vector<int> &indices,
                                          bool leadingSign) const {
  std::string e;
  for (int i : indices)
    e += l[i - 1] + l[i];

  return (leadingSign || !e.empty() && e[0] == '-') ? e : e.substr(1);
}

bool LinearSimplifier::isBitwiseWithBinop(const std::string &expr) const {
  bool hasArith = false, hasBitw = false;
  for (char ch : expr) {
    if (ch == '+' || ch == '*' || ch == '-')
      hasArith = true;
    if (ch == '&' || ch == '|' || ch == '^')
      hasBitw = true;
  }
  return !hasArith && hasBitw;
}

std::string LinearSimplifier::simplifyPartsAndCompose(
    const std::vector<std::string> &l, const std::vector<std::vector<int>> &partition,
    int constIdx, const std::vector<int> &lrem) {
  std::string simpl;

  for (const auto &part : partition) {
    std::string e = composeTerms(l, part, false);

    LinearSimplifier innerSimplifier(bitCount, e, modRed, refine, verifBitCount, metric);
    std::string s = innerSimplifier.simplifyImpl(false, true);

    if (constIdx != -1) {
      e += l[constIdx - 1] + l[constIdx];

      LinearSimplifier innerSimplifier2(bitCount, e, modRed, refine, verifBitCount,
                                        metric);
      std::string s2 = innerSimplifier2.simplifyImpl(false, true);

      if (countTerms(s) == countTerms(s2)) {
        s = s2;
        constIdx = -1;
      }
    }

    if (s.empty() || s == "0")
      continue;

    if (isBitwiseWithBinop(s))
      s = "(" + s + ")";

    if (!simpl.empty() && s[0] != '-')
      simpl += "+";

    simpl += s;
  }

  if (lrem.empty()) {
    if (constIdx != -1)
      simpl += l[constIdx - 1] + l[constIdx];

    return simpl.empty() ? "0" : simpl;
  }

  std::string e = composeTerms(l, lrem, false);
  if (constIdx != -1)
    e += l[constIdx - 1] + l[constIdx];

  if (!e.empty()) {
    LinearSimplifier innerSimplifier(bitCount, e, modRed, refine, verifBitCount, metric);
    std::string s = innerSimplifier.simplifyImpl(false, true);

    if (!s.empty() && s != "0") {
      if (isBitwiseWithBinop(s))
        s = "(" + s + ")";

      if (!simpl.empty() && s[0] != '-')
        simpl += "+";

      simpl += s;
    }
  }

  return simpl.empty() ? "0" : simpl;
}

void LinearSimplifier::trySplit() {
  auto l = splitIntoTerms(res);
  auto v = findVariablesInTerms(l);
  auto part = partitionTermsWrtVariableCount(l, v);
  int constIdx = std::get<0>(part);
  auto l1 = std::get<1>(part);
  auto l2 = std::get<2>(part);
  auto l3 = std::get<3>(part);
  auto lrem = std::get<4>(part);

  auto partitionRes = partition(v, l1, l2, l3, lrem);

  std::string e = simplifyPartsAndCompose(l, partitionRes, constIdx, lrem);
  checkSolutionComplexity(e);
}

std::string simplifyLinearMba(const std::string &expr, int bitCount, bool useZ3,
                              bool checkLinearFlag, bool modRed, bool refine,
                              int verifBitCount, Metric metric) {
  if (checkLinearFlag && !checkLinear(expr, bitCount))
    return "";

  LinearSimplifier simplifier(bitCount, expr, modRed, refine, verifBitCount, metric);
  if (!simplifier.valid)
    return "";

  return simplifier.simplify(useZ3);
}

} // namespace MBA
} // namespace LSiMBA
