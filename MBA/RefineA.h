// Refine batch A — member declarations (included inside class Node).
// Orchestrator + step-1 refinement + shared helper methods.

  // --------------------------------------------------------- orchestrator
  void refine(Node *parent = nullptr, bool restrictedScope = false);
  void refineStep1(bool restrictedScope = false);
  bool refineStep2(Node *parent = nullptr, bool restrictedScope = false);

  // --------------------------------------------------------- inspect constants
  void inspectConstants();
  void inspectConstantsInclDisjunction();
  void inspectConstantsExclDisjunction();
  void inspectConstantsConjunction();
  void inspectConstantsSum();
  void inspectConstantsProduct();
  void inspectConstantsNegation();
  void inspectConstantsPower();
  void inspectConstantsConstant();

  // --------------------------------------------------------- flatten
  void flatten();
  void flattenBinaryGeneric();
  void flattenProduct();

  // --------------------------------------------------------- duplicate children
  void checkDuplicateChildren();
  void removeDuplicateChildren();
  void removePairsOfChildren();
  void mergeSimilarNodesSum();
  bool isZeroProduct() const;
  bool hasFactorOne() const;
  bool getOptConstFactor(MBAValue &out) const;
  bool tryMergeSumChildren(int i, int j);
  bool equalsNeglectingConstants(const Node &other, bool hasConst, bool hasConstOther) const;
  bool equalsNeglectingConstantsOtherConst(const Node &other) const;
  bool equalsNeglectingConstantsBothConst(const Node &other) const;

  // --------------------------------------------------------- inverse nodes
  void resolveInverseNodes();
  void resolveInverseNodesBitwise();
  bool isBitwiseInverse(const Node &other) const;

  // --------------------------------------------------------- trivial nodes
  void removeTrivialNodes();

  // --------------------------------------------------------- nested negations
  bool eliminateNestedNegationsAdvanced();
