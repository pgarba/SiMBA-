// GAMBA native C++ port — Batch implementation. Mirrors batch.py.
#include "Batch.h"

#include <algorithm>

namespace LSiMBA {
namespace MBA {

Batch::Batch(const std::vector<IndexWithMultitude> &prevFactorIndices,
             const std::vector<IndexWithMultitude> &factorIndices,
             const std::set<int> &termIndices,
             std::vector<std::pair<int, IndexWithMultitudeSet>> &nodesToTerms,
             const std::vector<IndexWithMultitudeSet> &termsToNodes,
             const std::vector<bool> &nodesTriviality,
             const std::vector<int> &nodesOrder)
    : prevFactorIndices(prevFactorIndices), factorIndices(factorIndices) {
  partition(nodesToTerms, termIndices, termsToNodes, nodesTriviality, nodesOrder);
}

void Batch::partition(std::vector<std::pair<int, IndexWithMultitudeSet>> &nodesToTerms,
                      const std::set<int> &termIndices,
                      const std::vector<IndexWithMultitudeSet> &termsToNodes,
                      const std::vector<bool> &nodesTriviality,
                      const std::vector<int> &nodesOrder) {
  std::set<int> todo = termIndices;

  while (true) {
    if (nodesToTerms.empty())
      break;

    int idx = getNextBatch(nodesToTerms, termsToNodes, nodesTriviality, nodesOrder);
    if (idx == -1)
      break;

    int factor = nodesToTerms[idx].first;
    int64_t multitude = getLowestMultitude(nodesToTerms[idx].second);

    std::vector<IndexWithMultitude> factors = {IndexWithMultitude(factor, multitude)};
    std::set<int> terms;
    for (auto &p : nodesToTerms[idx].second)
      terms.insert(p.idx);

    std::vector<std::pair<int, IndexWithMultitudeSet>> ntt;

    nodesToTerms[idx].second = reduceMultitudes(nodesToTerms[idx].second, multitude);
    if (!nodesToTerms[idx].second.empty())
      ntt.push_back(nodesToTerms[idx]);
    nodesToTerms.erase(nodesToTerms.begin() + idx);

    for (int i = static_cast<int>(nodesToTerms.size()) - 1; i >= 0; --i) {
      auto &b = nodesToTerms[i];

      std::set<int> t;
      for (auto &p : b.second)
        t.insert(p.idx);
      std::set<int> inters;
      for (int x : t)
        if (terms.count(x))
          inters.insert(x);

      if (inters.size() > 1) {
        if (inters.size() == terms.size()) {
          IndexWithMultitudeSet spl;
          for (auto &p : b.second)
            if (inters.count(p.idx))
              spl.push_back(p);

          int64_t m2 = getLowestMultitude(spl);
          factors.push_back(IndexWithMultitude(b.first, m2));

          spl = reduceMultitudes(spl, m2);
          if (!spl.empty())
            ntt.push_back({b.first, spl});
        } else {
          IndexWithMultitudeSet part;
          for (auto &p : b.second)
            if (inters.count(p.idx))
              part.push_back(p);
          ntt.push_back({b.first, part});
        }

        IndexWithMultitudeSet rest;
        for (auto &p : b.second)
          if (!inters.count(p.idx))
            rest.push_back(p);
        b.second = rest;
        if (b.second.size() <= 1)
          nodesToTerms.erase(nodesToTerms.begin() + i);

      } else if (inters.size() > 0) {
        IndexWithMultitudeSet rest;
        for (auto &p : b.second)
          if (!inters.count(p.idx))
            rest.push_back(p);
        b.second = rest;
        if (b.second.size() <= 1)
          nodesToTerms.erase(nodesToTerms.begin() + i);
      }
    }

    std::vector<IndexWithMultitude> newPrev = factorIndices;
    for (auto &p : prevFactorIndices)
      newPrev.push_back(p);
    children.push_back(Batch(newPrev, factors, terms, ntt, termsToNodes, nodesTriviality,
                             nodesOrder));
    for (int x : terms)
      todo.erase(x);
  }

  atoms = todo;
}

int Batch::getNextBatch(const std::vector<std::pair<int, IndexWithMultitudeSet>> &nodesToTerms,
                        const std::vector<IndexWithMultitudeSet> &termsToNodes,
                        const std::vector<bool> &nodesTriviality,
                        const std::vector<int> &nodesOrder) const {
  std::vector<int> indices = getLargestTermsetIndices(nodesToTerms, &termsToNodes,
                                                     &nodesTriviality);
  if (indices.empty())
    indices = getLargestTermsetIndices(nodesToTerms);
  if (indices.empty())
    return -1;

  if (indices.size() == 1)
    return indices[0];

  auto collected = collectLargestBatches(nodesToTerms, indices);
  auto largest = getLargestListIndices(collected);
  for (int o : nodesOrder)
    if (std::find(largest.begin(), largest.end(), o) != largest.end())
      return indices[o];

  return indices[largest[0]];
}

std::vector<std::vector<int>> Batch::collectLargestBatches(
    const std::vector<std::pair<int, IndexWithMultitudeSet>> &nodesToTerms,
    const std::vector<int> &indicesIn) const {
  std::vector<std::vector<int>> collected;
  std::vector<int> indices = indicesIn;
  int i = 0;

  while (true) {
    if (i == static_cast<int>(indices.size()))
      break;

    bool found = false;
    for (int j = 0; j < i; ++j) {
      if (nodesToTerms[i].first == nodesToTerms[j].first &&
          nodesToTerms[i].second == nodesToTerms[j].second) {
        collected[j].push_back(i);
        indices.erase(indices.begin() + i);
        found = true;
        break;
      }
    }

    if (!found) {
      collected.push_back({i});
      i++;
    }
  }

  return collected;
}

int64_t Batch::getLowestMultitude(const IndexWithMultitudeSet &indicesWithMultitude) const {
  int64_t m = indicesWithMultitude[0].multitude;
  for (auto &p : indicesWithMultitude)
    m = std::min(m, p.multitude);
  return m;
}

IndexWithMultitudeSet Batch::reduceMultitudes(const IndexWithMultitudeSet &indicesWithMultitude,
                                             int64_t delta) const {
  IndexWithMultitudeSet t = indicesWithMultitude;
  for (auto &p : t)
    p.multitude -= delta;

  IndexWithMultitudeSet out;
  for (auto &p : t)
    if (p.multitude > 0)
      out.push_back(p);
  return out;
}

std::vector<int> Batch::getLargestTermsetIndices(
    const std::vector<std::pair<int, IndexWithMultitudeSet>> &pairs,
    const std::vector<IndexWithMultitudeSet> *termsToNodes,
    const std::vector<bool> *nodesTriviality) const {
  std::vector<int> indices;
  int maxl = -1;

  for (int i = 0; i < static_cast<int>(pairs.size()); ++i) {
    int l = static_cast<int>(pairs[i].second.size());
    if (l < 2)
      continue;

    if (maxl != -1 && l < maxl)
      continue;

    if (termsToNodes != nullptr) {
      if (!checkForNontrivial(pairs[i], *termsToNodes, *nodesTriviality))
        continue;
    }

    if (l == maxl)
      indices.push_back(i);
    else {
      indices = {i};
      maxl = l;
    }
  }

  return indices;
}

bool Batch::checkForNontrivial(const std::pair<int, IndexWithMultitudeSet> &nodeToTerms,
                               const std::vector<IndexWithMultitudeSet> &termsToNodes,
                               const std::vector<bool> &nodesTriviality) const {
  for (auto &pair : nodeToTerms.second) {
    IndexWithMultitudeSet t;
    for (auto &p : termsToNodes[pair.idx])
      t.push_back(IndexWithMultitude(p.idx, p.multitude));
    t = reduceMultitudesCorrespondingToList(t, factorIndices);
    t = reduceMultitudesCorrespondingToList(t, prevFactorIndices);

    for (auto &p : t) {
      if (p.idx == nodeToTerms.first)
        continue;
      if (!nodesTriviality[p.idx])
        return true;
    }
  }

  return false;
}

IndexWithMultitudeSet Batch::reduceMultitudesCorrespondingToList(
    const IndexWithMultitudeSet &indicesWithMultitude,
    const std::vector<IndexWithMultitude> &reductions) const {
  IndexWithMultitudeSet t = indicesWithMultitude;
  for (auto &r : reductions) {
    for (auto &p : t) {
      if (p.idx == r.idx) {
        p.multitude -= r.multitude;
        break;
      }
    }
  }

  IndexWithMultitudeSet out;
  for (auto &p : t)
    if (p.multitude > 0)
      out.push_back(p);
  return out;
}

std::vector<int> Batch::getLargestListIndices(const std::vector<std::vector<int>> &lists) const {
  std::vector<int> indices = {0};
  int maxl = static_cast<int>(lists[0].size());

  for (int i = 1; i < static_cast<int>(lists.size()); ++i) {
    int l = static_cast<int>(lists[i].size());
    if (l == maxl)
      indices.push_back(i);
    else if (l > maxl) {
      indices = {i};
      maxl = l;
    }
  }

  return indices;
}

} // namespace MBA
} // namespace LSiMBA
