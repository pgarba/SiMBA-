// GAMBA native C++ port — BitwiseFactory implementation.
#include "BitwiseFactory.h"

#include <algorithm>

#include "BitwiseList3Vars.h"
#include "Dnf.h"

namespace LSiMBA {
namespace MBA {

BitwiseFactory::BitwiseFactory(int vnumber,
                               const std::vector<std::string> *variables, bool noTable)
    : vnumber(vnumber), usesDefaultVars(variables == nullptr), noTable(noTable) {
  if (variables != nullptr)
    this->variables = *variables;
  else
    for (int i = 0; i < vnumber; ++i)
      this->variables.push_back(getAltVname(i));
}

std::string BitwiseFactory::getAltVname(int i) const {
  return "X[" + std::to_string(i) + "]";
}

void BitwiseFactory::initTable() {
  if (vnumber == 1)
    initTable1var();
  else if (vnumber == 2)
    initTable2vars();
  else if (vnumber == 3)
    initTable3vars();
}

void BitwiseFactory::initTable1var() {
  table = {"0", "X[0]"};
}

void BitwiseFactory::initTable2vars() {
  table = {"0",           "(X[0]&~X[1])", "(~X[0]&X[1])", "(X[0]^X[1])",
           "(X[0]&X[1])", "X[0]",         "X[1]",         "(X[0]|X[1])"};
}

void BitwiseFactory::initTable3vars() {
  table.assign(kBitwiseList3Vars, kBitwiseList3Vars + kBitwiseList3VarsCount);
}

std::string BitwiseFactory::createBitwiseImpl(const std::vector<int> &vector) {
  Dnf d(vnumber, vector);
  auto b = d.toBitwise();
  b->refine();
  std::string s = b->toString(variables);

  if (s.find('&') != std::string::npos || s.find('|') != std::string::npos ||
      s.find('^') != std::string::npos)
    s = "(" + s + ")";
  return s;
}

std::vector<int> BitwiseFactory::getBitwiseVector(const std::vector<int64_t> &vector,
                                                 int64_t offset) const {
  std::vector<int> out;
  for (int64_t v : vector)
    out.push_back(v == offset ? 0 : 1);
  return out;
}

int BitwiseFactory::getBitwiseIndexForVector(const std::vector<int64_t> &vector,
                                            int64_t offset) const {
  int index = 0;
  int add = 1;
  for (size_t i = 0; i + 1 < vector.size(); ++i) {
    if (vector[i + 1] != offset)
      index += add;
    add <<= 1;
  }
  return index;
}

std::string BitwiseFactory::getBitwiseFromTable(const std::vector<int64_t> &vector,
                                               int64_t offset) {
  if (!tableInit) {
    initTable();
    tableInit = true;
  }

  int index = getBitwiseIndexForVector(vector, offset);
  std::string bitwise = table[index];

  if (!usesDefaultVars) {
    for (int i = 0; i < vnumber; ++i) {
      std::string alt = getAltVname(i);
      size_t pos = 0;
      while ((pos = bitwise.find(alt, pos)) != std::string::npos) {
        bitwise.replace(pos, alt.size(), variables[i]);
        pos += variables[i].size();
      }
    }
  }

  return bitwise;
}

std::string BitwiseFactory::createBitwiseWithOffset(const std::vector<int64_t> &vector,
                                                  int64_t offset) {
  auto v = getBitwiseVector(vector, offset);
  return createBitwiseImpl(v);
}

std::string BitwiseFactory::createBitwiseUnnegated(const std::vector<int64_t> &vector,
                                                  int64_t offset) {
  if (!noTable && vnumber <= 3)
    return getBitwiseFromTable(vector, offset);
  return createBitwiseWithOffset(vector, offset);
}

std::string BitwiseFactory::createBitwise(std::vector<int64_t> vector, bool negated,
                                         int64_t offset) {
  if (!noTable && vnumber <= 3 && vector[0] != offset) {
    for (size_t i = 0; i < vector.size(); ++i)
      vector[i] = offset + (vector[i] - offset + 1) % 2;
    negated = true;
  }

  std::string e = createBitwiseUnnegated(vector, offset);
  if (negated)
    return e[0] == '~' ? e.substr(1) : "~" + e;
  return e;
}

} // namespace MBA
} // namespace LSiMBA
