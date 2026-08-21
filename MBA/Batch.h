// GAMBA native C++ port — Batch / IndexWithMultitude.
// Mirrors external/GAMBA/src/utils/batch.py.
#pragma once

#include <cstdint>
#include <set>
#include <vector>

namespace LSiMBA {
namespace MBA {

// An index together with a multitude it appears with.
struct IndexWithMultitude {
  int idx;
  int64_t multitude;
  IndexWithMultitude(int i, int64_t m = 1) : idx(i), multitude(m) {}
  bool operator==(const IndexWithMultitude &o) const {
    return idx == o.idx && multitude == o.multitude;
  }
};

// A "set" of IndexWithMultitude. Mirrors Python's identity-based set of
// IndexWithMultitude objects: each element is distinct and looked up by idx.
using IndexWithMultitudeSet = std::vector<IndexWithMultitude>;

// A node representing a subset of a sum.
class Batch {
 public:
  std::vector<IndexWithMultitude> prevFactorIndices;
  std::vector<IndexWithMultitude> factorIndices;
  std::set<int> atoms;
  std::vector<Batch> children;

  Batch(const std::vector<IndexWithMultitude> &prevFactorIndices,
        const std::vector<IndexWithMultitude> &factorIndices,
        const std::set<int> &termIndices,
        std::vector<std::pair<int, IndexWithMultitudeSet>> &nodesToTerms,
        const std::vector<IndexWithMultitudeSet> &termsToNodes,
        const std::vector<bool> &nodesTriviality,
        const std::vector<int> &nodesOrder);

  bool isTrivial() const { return children.empty(); }

 private:
  void partition(std::vector<std::pair<int, IndexWithMultitudeSet>> &nodesToTerms,
                 const std::set<int> &termIndices,
                 const std::vector<IndexWithMultitudeSet> &termsToNodes,
                 const std::vector<bool> &nodesTriviality,
                 const std::vector<int> &nodesOrder);
  int getNextBatch(const std::vector<std::pair<int, IndexWithMultitudeSet>> &nodesToTerms,
                   const std::vector<IndexWithMultitudeSet> &termsToNodes,
                   const std::vector<bool> &nodesTriviality,
                   const std::vector<int> &nodesOrder) const;
  std::vector<std::vector<int>> collectLargestBatches(
      const std::vector<std::pair<int, IndexWithMultitudeSet>> &nodesToTerms,
      const std::vector<int> &indices) const;
  int64_t getLowestMultitude(const IndexWithMultitudeSet &indicesWithMultitude) const;
  IndexWithMultitudeSet reduceMultitudes(const IndexWithMultitudeSet &indicesWithMultitude,
                                        int64_t delta) const;
  std::vector<int> getLargestTermsetIndices(
      const std::vector<std::pair<int, IndexWithMultitudeSet>> &pairs,
      const std::vector<IndexWithMultitudeSet> *termsToNodes = nullptr,
      const std::vector<bool> *nodesTriviality = nullptr) const;
  bool checkForNontrivial(const std::pair<int, IndexWithMultitudeSet> &nodeToTerms,
                          const std::vector<IndexWithMultitudeSet> &termsToNodes,
                          const std::vector<bool> &nodesTriviality) const;
  IndexWithMultitudeSet reduceMultitudesCorrespondingToList(
      const IndexWithMultitudeSet &indicesWithMultitude,
      const std::vector<IndexWithMultitude> &reductions) const;
  std::vector<int> getLargestListIndices(const std::vector<std::vector<int>> &lists) const;
};

} // namespace MBA
} // namespace LSiMBA
