// Substitution — member declarations (included inside class Node).
// Mirrors external/GAMBA/src/utils/node.py substitution methods.

  // --------------------------------------------------------- substitution
  int getIndexInList(const std::vector<std::shared_ptr<Node>> &l) const;
  bool isContained(const std::vector<std::shared_ptr<Node>> &l) const;
  std::shared_ptr<Node> getNodeForSubstitution(
      const std::vector<std::shared_ptr<Node>> &ignoreList);
  bool substituteAllOccurences(const std::shared_ptr<Node> &node,
                               const std::string &vname, bool onlyFullMatch = false,
                               bool withMod = true);
  std::pair<bool, bool> trySubstituteNode(const std::shared_ptr<Node> &node,
                                          const std::string &vname, bool onlyFull,
                                          bool inverse = false);
  bool trySubstitutePartOfSum(const std::shared_ptr<Node> &node,
                              const std::string &vname, bool inverse = false);
  bool trySubstitutePartOfSumInSum(const std::shared_ptr<Node> &node,
                                   const std::string &vname, bool inverse);
  bool trySubstitutePartOfSumTerm(const std::shared_ptr<Node> &node,
                                  const std::string &vname, bool inverse);
  std::vector<std::shared_ptr<Node>> getCommonChildren(const Node &other) const;
  void removeChildrenOfNode(const Node &other);
