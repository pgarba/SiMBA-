// Refine batch C — member declarations (included inside class Node).
// Conjunction/disjunction identity rules.

  // --------------------------------------------------------- fixed true/false
  bool insertFixedInConj();
  bool checkInsertFixedTrue(const std::shared_ptr<Node> &node);
  bool insertFixedInDisj();
  bool checkInsertFixedFalse(const std::shared_ptr<Node> &node);

  // --------------------------------------------------------- trivial xor / xor-same
  bool checkTrivialXor();
  bool checkXorSameMultByMinusOne();

  // --------------------------------------------------------- conj zero / neg-xor
  bool checkConjZeroRule();
  bool hasConjZeroRule();
  bool checkConjNegXorZeroRule();
  bool hasConjNegXorZeroRule();
  std::shared_ptr<Node> getOptArgNegXorSameNeg();
  bool checkConjNegXorMinusOneRule();
  bool hasDisjNegXorMinusOneRule();
  bool checkConjNegatedXorZeroRule();
  bool hasConjNegatedXorZeroRule();
  std::shared_ptr<Node> getOptArgNegatedXorSameNeg();

  // --------------------------------------------------------- xor identity
  bool checkConjXorIdentityRule();
  bool isXorSameNeg();
  bool isDouble(const std::shared_ptr<Node> &node);
  bool checkDisjXorIdentityRule();
  std::shared_ptr<Node> getOptXorDisjXorIdentity();

  // --------------------------------------------------------- neg-conj identity
  bool checkConjNegConjIdentityRule();
  std::shared_ptr<Node> getOptArgNegConjDouble();
  std::shared_ptr<Node> getOptArgNegConjDouble1();
  std::shared_ptr<Node> getOptArgNegConjDouble2();

  // --------------------------------------------------------- nested bitwise identity
  bool checkDisjDisjIdentityRule();
  bool checkConjConjIdentityRule();
  bool checkNestedBitwiseIdentityRule(NodeType t);
  std::vector<std::shared_ptr<Node>> getCandidatesNestedBitwiseIdentity(NodeType t);

  // --------------------------------------------------------- disj-conj identity
  bool checkDisjConjIdentityRule();
  std::shared_ptr<Node> getOptArgDisjConjIdentity();
  std::shared_ptr<Node> getOptArgDisjConjIdentity1();
  std::shared_ptr<Node> getOptArgDisjConjIdentity2();
  std::shared_ptr<Node> divided(int64_t divisor);
  bool checkDisjConjIdentityRule2();
  std::shared_ptr<Node> getOptArgDisjConjIdentityRule2();

  // --------------------------------------------------------- disj-neg-disj identity
  bool checkDisjNegDisjIdentityRule();
  std::shared_ptr<Node> getOptArgDisjNegDisjIdentityRule();

  // --------------------------------------------------------- conj-disj identity
  bool checkConjDisjIdentityRule();
  std::shared_ptr<Node> getOptArgConjDisjIdentity();
  std::shared_ptr<Node> getOptArgConjDisjIdentity1();
  std::shared_ptr<Node> getOptArgConjDisjIdentity2();

  // --------------------------------------------------------- sub identities
  bool checkDisjSubDisjIdentityRule();
  std::shared_ptr<Node> getOptArgDisjSubDisjIdentity();
  bool checkDisjSubConjIdentityRule();
  std::shared_ptr<Node> getOptArgDisjSubConjIdentity();
  bool checkConjAddConjIdentityRule();
  std::shared_ptr<Node> getOptArgConjAddConjIdentity();

  // --------------------------------------------------------- nested bitwise rule
  bool checkConjConjDisjRule();
  bool checkDisjDisjConjRule();
  bool checkNestedBitwiseRule(NodeType t);
  std::pair<std::shared_ptr<Node>, std::shared_ptr<Node>> getOptArgNestedBitwise(NodeType t);

  bool checkDisjDisjConjRule2();
  std::pair<std::shared_ptr<Node>, std::shared_ptr<Node>> getOptPairDisjDisjConj2();
