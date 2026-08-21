// GAMBA native C++ port — Dnf implementation.
#include "Dnf.h"

#include <set>

namespace LSiMBA {
namespace MBA {

namespace {
int popcount(int x) {
  int c = 0;
  while (x) {
    c += x & 1;
    x >>= 1;
  }
  return c;
}
} // namespace

Dnf::Dnf(int vnumber, const std::vector<int> &vec) {
  initGroups(vnumber, vec);
  merge();
  dropUnrequiredImplicants(vec);
}

void Dnf::initGroups(int vnumber, const std::vector<int> &vec) {
  groups.assign(vnumber + 1, {});
  for (int i = 0; i < static_cast<int>(vec.size()); ++i) {
    int bit = vec[i];
    if (bit == 0)
      continue;

    auto impl = std::make_shared<Implicant>(vnumber, i);
    int onesCnt = popcount(i);
    groups[onesCnt][0].push_back(impl);
  }
}

bool Dnf::mergeStep() {
  bool changed = false;
  std::vector<std::unordered_map<int, std::vector<std::shared_ptr<Implicant>>>> newGroups(
      groups.size());

  for (size_t onesCnt = 0; onesCnt < groups.size(); ++onesCnt) {
    auto &group = groups[onesCnt];

    if (onesCnt < groups.size() - 1) {
      auto &nextGroup = groups[onesCnt + 1];

      for (auto &kv : group) {
        auto it = nextGroup.find(kv.first);
        if (it == nextGroup.end())
          continue;

        for (auto &impl1 : kv.second) {
          for (auto &impl2 : it->second) {
            auto newImpl = impl1->tryMerge(*impl2);
            if (newImpl == nullptr)
              continue;

            changed = true;
            impl1->obsolete = true;
            impl2->obsolete = true;

            int newH = newImpl->getIndifferentHash();
            newGroups[newImpl->countOnes()][newH].push_back(newImpl);
          }
        }
      }
    }

    for (auto &kv : group) {
      for (auto &impl : kv.second) {
        if (!impl->obsolete)
          primes.push_back(impl);
      }
    }
  }

  groups = std::move(newGroups);
  if (!groups.empty() && groups.back().empty())
    groups.pop_back();

  return changed;
}

void Dnf::merge() {
  while (true) {
    if (!mergeStep())
      return;
  }
}

void Dnf::dropUnrequiredImplicants(const std::vector<int> &vec) {
  std::set<int64_t> requ;
  for (int64_t i = 0; i < static_cast<int64_t>(vec.size()); ++i)
    if (vec[i] == 1)
      requ.insert(i);

  size_t i = 0;
  while (i < primes.size()) {
    auto &impl = primes[i];
    std::set<int64_t> mtSet(impl->minterms.begin(), impl->minterms.end());

    bool intersect = false;
    for (int64_t m : mtSet)
      if (requ.count(m)) {
        intersect = true;
        break;
      }

    if (intersect) {
      for (int64_t m : mtSet)
        requ.erase(m);
      i++;
      continue;
    }

    primes.erase(primes.begin() + i);
  }
}

std::shared_ptr<Bitwise> Dnf::toBitwise() const {
  int cnt = static_cast<int>(primes.size());
  if (cnt == 0)
    return std::make_shared<Bitwise>(BitwiseType::TRUE, true);
  if (cnt == 1)
    return primes[0]->toBitwise();

  auto root = std::make_shared<Bitwise>(BitwiseType::INCL_DISJUNCTION);
  for (auto &p : primes)
    root->addChild(p->toBitwise());
  return root;
}

std::string Dnf::get(const std::vector<std::string> &variables) const {
  if (primes.empty())
    return "0";

  std::string s;
  for (auto &p : primes) {
    if (!s.empty())
      s += "|";
    std::string ps = p->get(variables);
    bool withPar = primes.size() > 1 && ps.find("&") != std::string::npos;
    s += withPar ? "(" + ps + ")" : ps;
  }
  return s;
}

} // namespace MBA
} // namespace LSiMBA
