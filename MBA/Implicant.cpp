// GAMBA native C++ port — Implicant implementation.
#include "Implicant.h"

namespace LSiMBA {
namespace MBA {

Implicant::Implicant(int vnumber, int64_t value) {
  obsolete = false;
  if (value != -1) {
    minterms.push_back(value);
    initVec(vnumber, value);
  }
}

void Implicant::initVec(int vnumber, int64_t value) {
  for (int i = 0; i < vnumber; ++i) {
    vec.push_back(static_cast<int>(value & 1));
    value >>= 1;
  }
}

int Implicant::countOnes() const {
  int cnt = 0;
  for (int v : vec)
    if (v == 1)
      cnt++;
  return cnt;
}

std::shared_ptr<Implicant> Implicant::tryMerge(const Implicant &other) const {
  int diffIdx = -1;
  for (size_t i = 0; i < vec.size(); ++i) {
    if (vec[i] == other.vec[i])
      continue;
    if (diffIdx != -1)
      return nullptr;
    diffIdx = static_cast<int>(i);
  }

  auto newImpl = getCopy();
  for (int64_t m : other.minterms)
    newImpl->minterms.push_back(m);
  if (diffIdx != -1)
    newImpl->vec[diffIdx] = -1;

  return newImpl;
}

int Implicant::getIndifferentHash() const {
  int h = 0;
  int n = 1;
  for (int v : vec) {
    if (v != 1)
      h += n;
    n <<= 1;
  }
  return h;
}

std::shared_ptr<Bitwise> Implicant::toBitwise() const {
  auto root = std::make_shared<Bitwise>(BitwiseType::CONJUNCTION);
  for (size_t i = 0; i < vec.size(); ++i) {
    if (vec[i] == -1)
      continue;
    root->addVariable(static_cast<int>(i), vec[i] == 0);
  }

  int cnt = root->childCount();
  if (cnt == 0)
    return std::make_shared<Bitwise>(BitwiseType::TRUE);
  if (cnt == 1)
    return root->firstChild();
  return root;
}

std::string Implicant::get(const std::vector<std::string> &variables) const {
  std::string s;
  for (size_t i = 0; i < vec.size(); ++i) {
    if (vec[i] == -1)
      continue;
    if (!s.empty())
      s += "&";
    if (vec[i] == 0)
      s += "~";
    s += variables[i];
  }
  return s.empty() ? "-1" : s;
}

std::shared_ptr<Implicant> Implicant::getCopy() const {
  auto cpy = std::make_shared<Implicant>(static_cast<int>(vec.size()), -1);
  cpy->vec = vec;
  cpy->minterms = minterms;
  return cpy;
}

} // namespace MBA
} // namespace LSiMBA
