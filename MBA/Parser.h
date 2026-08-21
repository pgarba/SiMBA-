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
