// GAMBA native C++ port — Dnf (Quine-McCluskey).
// Mirrors external/GAMBA/src/bitwise-factory/utils/dnf.py.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Bitwise.h"
#include "Implicant.h"

namespace LSiMBA {
namespace MBA {

// A disjunctive normal form: a disjunction of conjunctions of possibly negated
// variables.
class Dnf {
 public:
  std::vector<std::shared_ptr<Implicant>> primes;

  Dnf(int vnumber, const std::vector<int> &vec);

  std::shared_ptr<Bitwise> toBitwise() const;
  std::string get(const std::vector<std::string> &variables) const;

 private:
  std::vector<std::unordered_map<int, std::vector<std::shared_ptr<Implicant>>>> groups;

  void initGroups(int vnumber, const std::vector<int> &vec);
  bool mergeStep();
  void merge();
  void dropUnrequiredImplicants(const std::vector<int> &vec);
};

} // namespace MBA
} // namespace LSiMBA
