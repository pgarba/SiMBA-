// Refine batch D — member declarations (included inside class Node).
// Mirrors external/GAMBA/src/utils/node.py (bitwise-in-sums rules,
// post-substitution refinement, polish, verification).

  // --------------------------------------------------------- substitution
  void replaceVariable(const std::string &vname, const std::shared_ptr<Node> &node);

  // --------------------------------------------------------- polish
  void polish(Node *parent = nullptr);
  void resolveBitwiseNegationsInSums();
  int countChildrenMultByMinusOne() const;
  void insertBitwiseNegations(Node *parent);
  std::pair<std::shared_ptr<Node>, int64_t> getOptTransformedNegatedWithFactor();

  // --------------------------------------------------- verify via evaluation
  bool checkVerify(const std::shared_ptr<Node> &other, int bitCount = 2);

  // ------------------------------------------------- refine after substitution
  bool refineAfterSubstitution();
  bool checkBitwiseInSumsCancelTerms();
  int checkTransformBitwiseInSumCancel(int idx, const std::shared_ptr<Node> &bitw,
                                       int64_t factor);
  int checkTransformBitwiseInSumCancelImpl(bool toXor, int idx,
                                           const std::shared_ptr<Node> &bitw,
                                           int64_t factor);
  int checkTransformBitwiseForComb(bool toXor, int idx,
                                   const std::shared_ptr<Node> &bitw, int64_t factor,
                                   const std::shared_ptr<Node> &opSum, int combIdx);
  int checkTransformBitwiseForDiff(bool toXor, int idx,
                                   const std::shared_ptr<Node> &bitw, int64_t factor,
                                   const std::shared_ptr<Node> &diff,
                                   const std::vector<int> &indices);
  int checkTransformBitwiseForDiffFull(bool toXor, int idx,
                                       const std::shared_ptr<Node> &bitw, int64_t factor,
                                       const std::shared_ptr<Node> &diff,
                                       const std::vector<int> &indices);
  int checkTransformBitwiseForDiffMerge(bool toXor, int idx,
                                        const std::shared_ptr<Node> &bitw, int64_t factor,
                                        const std::shared_ptr<Node> &diff,
                                        const std::vector<int> &indices);
  NodeType getTransformedBitwiseType(bool toXor) const;
  bool checkBitwiseInSumsReplaceTerms();
  int checkTransformBitwiseInSumReplace(int idx, const std::shared_ptr<Node> &bitw,
                                        int64_t factor);
  int getIndexOfMoreComplexOperand();
  int checkTransformBitwiseInSumReplaceImpl(bool toXor, int idx,
                                            const std::shared_ptr<Node> &bitw, int cIdx,
                                            int64_t factor);
  int checkTransformBitwiseReplaceForComb(bool toXor, int idx,
                                          const std::shared_ptr<Node> &bitw, int64_t factor,
                                          const std::shared_ptr<Node> &cOp, int cIdx,
                                          int combIdx);
  int checkTransformBitwiseReplaceForDiff(bool toXor, int idx,
                                          const std::shared_ptr<Node> &bitw, int64_t factor,
                                          const std::shared_ptr<Node> &diff, int cIdx,
                                          const std::vector<int> &indices);
  int checkTransformBitwiseReplaceForDiffFull(bool toXor, int idx,
                                              const std::shared_ptr<Node> &bitw, int64_t factor,
                                              const std::shared_ptr<Node> &diff, int cIdx,
                                              const std::vector<int> &indices);
  bool checkDisjInvolvingXorInSums();
  bool checkXorInvolvingDisj();
  bool checkNegativeBitwInverse();

  // ------------------------------------------------------- xor/bitw with constants
  bool checkXorPairsWithConstants();
  std::vector<std::pair<int64_t, std::vector<int>>>
  collectIndicesOfBitwWithConstantsInSum(NodeType expType);
  // Returns the factor and the bitwise node; node is null if there is none.
  // The node pointer aliases `this` or one of its children (kept alive by the
  // parent's children list).
  std::pair<int64_t, const Node *>
  getFactorOfBitwWithConstant(NodeType expType = NodeType::CONSTANT) const;
  bool checkBitwPairsWithConstants();
  bool checkBitwPairsWithConstantsImpl(bool conj);
  bool checkDiffBitwPairsWithConstants();
  std::vector<std::vector<std::pair<int64_t, int>>> collectAllIndicesOfBitwWithConstants();
  bool getFactorForMergingBitwise(int64_t fac1, int64_t fac2, NodeType type1, NodeType type2,
                                  int64_t &out) const;
  std::tuple<int64_t, bool, int64_t> mergeBitwiseTerms(
      int firstIdx, int secIdx, const std::shared_ptr<Node> &first,
      const std::shared_ptr<Node> &second, int64_t factor, int64_t firstConst,
      int64_t secConst);
  std::tuple<int64_t, int64_t, int64_t> mergeBitwiseTermsAndGetOpfactor(
      int firstIdx, int secIdx, const std::shared_ptr<Node> &first,
      const std::shared_ptr<Node> &second, int64_t factor, int64_t firstConst,
      int64_t secConst);
  int64_t getConstOperandForMergingBitwise(int64_t constSum, NodeType type1,
                                          NodeType type2) const;
  int64_t getBitwiseFactorForMergingBitwise(int64_t factor, NodeType type1,
                                           NodeType type2) const;
  std::pair<int64_t, int64_t> getOperandFactorAndConstantForMergingBitwise(
      int64_t factor, NodeType type1, NodeType type2, int64_t const1, int64_t const2) const;
  bool checkBitwTuplesWithConstants();
  bool tryMergeBitwiseWithConstantsWith2Others(
      std::vector<std::pair<int64_t, int>> &sublist, int i,
      std::vector<int> &toRemove, int64_t &add);
  bool tryMergeTripleBitwiseWithConstants(
      std::vector<std::pair<int64_t, int>> &sublist, int i, int j, int k,
      std::vector<int> &toRemove, int64_t &add);
  bool getFactorsForMergingTriple(NodeType type1, NodeType type2, NodeType type0,
                                 int64_t fac1, int64_t fac2, int64_t fac0, int64_t const1,
                                 int64_t const2, int64_t const0, int64_t &factor1,
                                 int64_t &factor2) const;
  bool getPossibleFactorForMergingBitwise(int64_t fac1, NodeType type1, NodeType type0,
                                         int64_t &out) const;

  // --------------------------------------------------- bitwise pairs with inverses
  bool checkBitwPairsWithInverses();
  bool checkBitwPairsWithInversesImpl(NodeType expType);
  std::vector<std::tuple<int64_t, int, std::vector<int>>>
  collectIndicesOfBitwWithoutConstantsInSum(NodeType expType);
  // Returns the factor and the bitwise node; node is null if there is none.
  std::pair<int64_t, const Node *>
  getFactorOfBitwWithoutConstant(NodeType expType = NodeType::CONSTANT) const;
  // Returns (idx1, idx2) or (-1, -1) if there is no single differing child.
  std::pair<int, int> getOnlyDifferingChildIndices(const Node &other) const;
  std::pair<int, int> getOnlyDifferingChildIndicesSameLen(const Node &other) const;
  std::pair<int, int> getOnlyDifferingChildIndicesDiffLen(const Node &other) const;
  std::tuple<bool, bool, int64_t> mergeInverseBitwiseTerms(
      int firstIdx, int secIdx, const std::shared_ptr<Node> &first,
      const std::shared_ptr<Node> &second, int64_t factor,
      const std::pair<int, int> &indices);
  std::tuple<int64_t, int64_t, int64_t>
  getOperandFactorsAndConstantForMergingInverseBitwise(int64_t factor, NodeType type1,
                                                     NodeType type2) const;
  bool mustInvertAtMergingInverseBitwise(NodeType type1, NodeType type2) const;
  bool checkDiffBitwPairsWithInverses();
  std::vector<std::pair<int64_t, int>> collectAllIndicesOfBitwWithoutConstants();

  // ------------------------------------------------------- bitwise and / xor in sum
  bool checkBitwAndOpInSum();
  bool checkInsertXorInSum();
