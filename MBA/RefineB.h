// Refine batch B — member declarations (included inside class Node).
// Bitwise-negation, power-of-two, beautify, rewrite-powers, factor-out rules.

  // --------------------------------------------------------- bitwise negations
  bool checkBitwiseNegations(Node *parent = nullptr);
  void substituteBitwiseNegationProduct();
  void substituteBitwiseNegationSum();
  bool substituteBitwiseNegationGeneric(Node *parent);

  // --------------------------------------------------------- powers of two
  bool checkBitwisePowersOfTwo();
  int getMaxFactorPowerOfTwoInChildren(bool allowRem = true);
  int getMaxFactorPowerOfTwo(bool allowRem);
  uint64_t divideByPowerOfTwo(int e);

  // --------------------------------------------------------- beautify constants
  bool checkBeautifyConstantsInProducts();
  bool checkBeautifyConstants(int e);

  // --------------------------------------------------------- move in negations
  bool checkMoveInBitwiseNegations();
  bool checkMoveInBitwiseNegationConjOrInclDisj();
  bool isAnyChildNegated();
  void negateAllChildren();
  void negate();
  bool checkMoveInBitwiseNegationExclDisj();
  Node *getRecursivelyNegatedChild(int *depth = nullptr, int maxDepth = -1);

  bool isNegated();

  // --------------------------------------------------------- excl disjunction negations
  bool checkBitwiseNegationsInExclDisjunctions();

  // --------------------------------------------------------- rewrite powers
  bool checkRewritePowers(Node *parent = nullptr);

  // --------------------------------------------------------- resolve product of powers
  bool checkResolveProductOfPowers();

  // --------------------------------------------------------- add
  void add(const std::shared_ptr<Node> &other);
  void addConstant(int64_t constant);
  void addToSum(const std::shared_ptr<Node> &other);

  // --------------------------------------------------------- product of constant and sum
  bool checkResolveProductOfConstantAndSum();

  // --------------------------------------------------------- factor out of sum
  bool checkFactorOutOfSum();
  std::shared_ptr<Node> tryFactorOutOfSum();
  std::shared_ptr<Node> getCommonFactorInSum();
  bool hasFactorInRemainingChildren(const std::shared_ptr<Node> &factor);
  bool hasFactor(const std::shared_ptr<Node> &factor);
  bool hasFactorProduct(const std::shared_ptr<Node> &factor);
  bool hasChild(const Node &node) const;
  int getIndexOfChild(const std::shared_ptr<Node> &node) const;
  int getIndexOfChildNegated(const std::shared_ptr<Node> &node) const;
  void eliminateFactor(const std::shared_ptr<Node> &factor);
  void eliminateFactorProduct(const std::shared_ptr<Node> &factor);
  void eliminateFactorPower(const std::shared_ptr<Node> &factor);
  void decrementExponent();
  void decrement();

  // --------------------------------------------------------- inverse negations in sum
  bool checkResolveInverseNegationsInSum();

  // --------------------------------------------------------- differing child indices
  bool getOnlyDifferingChildIndices(const Node &other, int *firstIdx, int *secIdx) const;
  bool getOnlyDifferingChildIndicesSameLen(const Node &other, int *firstIdx, int *secIdx) const;
  bool getOnlyDifferingChildIndicesDiffLen(const Node &other, int *firstIdx, int *secIdx) const;
