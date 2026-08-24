// GAMBA native C++ port — recursive-descent expression parser.
// Mirrors external/GAMBA/src/utils/parse.py (class Parser, parse()).
#ifndef MBA_PARSER_H
#define MBA_PARSER_H

#include <memory>
#include <string>

#include "Node.h"

namespace LSiMBA {
namespace MBA {

class Parser {
public:
  Parser(const std::string &expr, int bitCount, bool modRed = false)
      : expr(expr), bitCount(bitCount), modRed(modRed) {}

  // Parse the whole expression; returns nullptr (and sets error()) on failure.
  std::shared_ptr<Node> parseExpression();

  const std::string &error() const { return error_; }

private:
  std::string expr;
  int bitCount;
  bool modRed;
  size_t idx = 0;
  std::string error_;

  std::shared_ptr<Node> newNode(NodeType t) {
    return std::make_shared<Node>(t, bitCount, modRed);
  }

  // Grammar (low -> high precedence):
  //   |  ^  &  <<  + -  *  (unary ~ / unary -)  **  terminal
  std::shared_ptr<Node> parseInclusiveDisjunction();
  std::shared_ptr<Node> parseExclusiveDisjunction();
  std::shared_ptr<Node> parseConjunction();
  std::shared_ptr<Node> parseShift();
  std::shared_ptr<Node> parseSum();
  std::shared_ptr<Node> parseProduct();
  std::shared_ptr<Node> parseFactor();
  std::shared_ptr<Node> parseBitwiseNegatedExpression();
  std::shared_ptr<Node> parseNegativeExpression();
  std::shared_ptr<Node> multiplyByMinusOne(std::shared_ptr<Node> node);
  std::shared_ptr<Node> parsePower();
  std::shared_ptr<Node> parseTerminal();
  std::shared_ptr<Node> parseVariable();
  std::shared_ptr<Node> parseConstant();
  std::shared_ptr<Node> parseBinaryConstant();
  std::shared_ptr<Node> parseHexConstant();
  std::shared_ptr<Node> parseDecimalConstant();
  MBAValue getConstant(size_t start, int base);

  // ------------------------------------------------- div/rem desugaring
  // True iff the node is a plain (non bit-sliced) variable.
  bool isFullVariable(const Node &n) const;
  // Returns k iff the constant equals 2**k for some 0 <= k <= 63, else -1.
  int pow2Exponent(const MBAValue &v) const;
  // Build the node for "name[i] * 2**shift" (shift >= 0); shift == 0 is just
  // the bit variable name[i].
  std::shared_ptr<Node> bitTerm(const std::string &name, int i, int shift);
  // Desugar "base >> k" / "base / 2**k" / "base % 2**k" into a sum of bit
  // terms; sets error() and returns nullptr when unsupported.
  std::shared_ptr<Node>
  desugarDivRem(std::shared_ptr<Node> base, std::shared_ptr<Node> op,
                const std::string &kind);

  // -------------------------------------------------------- lexing helpers
  char peek() const;
  char peekNext() const;
  char get(bool skipSpaces = true);
  void skipSpace();
  bool hasSpace() const;
  bool hasBitwiseNegatedExpression() const;
  bool hasNegativeExpression() const;
  bool hasMultiplicator() const;
  bool hasPower() const;
  bool hasLshift() const;
  bool hasRshift() const;
  bool hasDiv() const;
  bool hasRem() const;
  bool hasBinaryConstant() const;
  bool hasHexConstant() const;
  bool hasHexDigit() const;
  bool hasDecimalDigit() const;
  bool hasVariable() const;
  bool hasLetter() const;
};

// Parse the given expression in a modular field with given bit count.
// Mirrors parse.py parse().
std::shared_ptr<Node>
parse(const std::string &expr, int bitCount, bool reduceConstants = true,
      bool refine = false, bool markLinear = false);

} // namespace MBA
} // namespace LSiMBA

#endif // MBA_PARSER_H
