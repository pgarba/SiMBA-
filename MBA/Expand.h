// Expand / factorize — member declarations (included inside class Node).
// Mirrors external/GAMBA/src/utils/node.py expand / factorize_sums.

  // --------------------------------------------------------- expand
  bool expand(bool restrictedScope = false);
  bool checkExpand();
  bool checkExpandProduct();
  bool hasSumChild() const;
  int getFirstSumIndex() const;
  void expandProduct();
  void multiplySum(const std::shared_ptr<Node> &other);
  void multiplySumWithSum(const std::shared_ptr<Node> &other, bool keepSum = false);
  std::shared_ptr<Node> getProductWithNode(const Node &other) const;
  std::shared_ptr<Node> getProductOfConstantAndNode(const Node &other) const;
  std::shared_ptr<Node> getProductOfProducts(const Node &other) const;
  void mergePowerIntoProduct(const Node &other);
  std::shared_ptr<Node> getProductOfPowers(const Node &other) const;
  std::shared_ptr<Node> getProductOfProductAndPower(const Node &other) const;
  std::shared_ptr<Node> getProductOfProductAndOther(const Node &other) const;
  std::shared_ptr<Node> getProductOfPowerAndOther(const Node &other) const;
  std::shared_ptr<Node> getProductGeneric(const Node &other) const;
  void multiplyWithNodeNoSum(const std::shared_ptr<Node> &other);
  void multiplyProductWithProduct(const std::shared_ptr<Node> &other);
  void multiplyPowerWithPower(const std::shared_ptr<Node> &other);
  void multiplyProductWithPower(const std::shared_ptr<Node> &other);
  void multiplyProductWithOther(const std::shared_ptr<Node> &other);
  void multiplyPowerWithOther(const std::shared_ptr<Node> &other);
  void multiplyGeneric(const std::shared_ptr<Node> &other);
  bool checkExpandPower();
  void expandPower(int64_t exp);

  // --------------------------------------------------------- factorize sums
  bool factorizeSums(bool restrictedScope = false);
  bool checkFactorizeSum();
  std::tuple<std::vector<std::shared_ptr<Node>>,
             std::vector<std::pair<int, IndexWithMultitudeSet>>,
             std::vector<IndexWithMultitudeSet>>
  collectAllFactorsOfSum();
  void collectFactors(int i, int64_t multitude,
                     std::vector<std::shared_ptr<Node>> &nodes,
                     std::vector<std::pair<int, IndexWithMultitudeSet>> &nodesToTerms,
                     std::vector<IndexWithMultitudeSet> &termsToNodes);
  void collectFactorsOfPower(int i, int64_t multitude,
                            std::vector<std::shared_ptr<Node>> &nodes,
                            std::vector<std::pair<int, IndexWithMultitudeSet>> &nodesToTerms,
                            std::vector<IndexWithMultitudeSet> &termsToNodes);
  void checkStoreFactor(int i, int64_t multitude,
                        std::vector<std::shared_ptr<Node>> &nodes,
                        std::vector<std::pair<int, IndexWithMultitudeSet>> &nodesToTerms,
                        std::vector<IndexWithMultitudeSet> &termsToNodes);
  std::vector<bool> determineNodesTriviality(const std::vector<std::shared_ptr<Node>> &nodes);
  std::vector<int> determineNodesOrder(const std::vector<std::shared_ptr<Node>> &nodes);
  bool isTrivialInFactorization() const;
  std::shared_ptr<Node> nodeFromBatch(const Batch &batch,
                                     const std::vector<std::shared_ptr<Node>> &nodes,
                                     std::vector<IndexWithMultitudeSet> &termsToNodes);
  void reduceNodeSet(IndexWithMultitudeSet &indicesWithMultitudes,
                     const std::vector<IndexWithMultitude> &l1,
                     const std::vector<IndexWithMultitude> &l2);
  int64_t getConstFactorRespectingPowers() const;
  std::shared_ptr<Node> createNodeForFactor(
      const std::vector<std::shared_ptr<Node>> &nodes,
      const IndexWithMultitude &indexWithMultitude);
