// GAMBA native C++ port — Implicant.
// Mirrors external/GAMBA/src/bitwise-factory/utils/implicant.py.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Bitwise.h"

namespace LSiMBA {
namespace MBA {

// A conjunction of possibly negated variables. vec[i] is 1 if the i-th variable
// occurs unnegatedly, 0 if negatedly, -1 (None) if it has no influence.
class Implicant {
 public:
  // vec entry: 1 = unnegated, 0 = negated, -1 = no influence.
  std::vector<int> vec;
  std::vector<int64_t> minterms;
  bool obsolete = false;

  Implicant(int vnumber, int64_t value);

  int countOnes() const;
  std::shared_ptr<Implicant> tryMerge(const Implicant &other) const;
  int getIndifferentHash() const;
  std::shared_ptr<Bitwise> toBitwise() const;
  std::string get(const std::vector<std::string> &variables) const;

 private:
  void initVec(int vnumber, int64_t value);
  std::shared_ptr<Implicant> getCopy() const;
};

} // namespace MBA
} // namespace LSiMBA
