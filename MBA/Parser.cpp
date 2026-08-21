// GAMBA native C++ port — Parser implementation.
#include "Parser.h"

#include <cctype>

namespace LSiMBA {
namespace MBA {

using namespace std;

namespace {
// Parse a digit string in the given base (2, 10, or 16) into a 128-bit
// (unsigned) value, so that 64-bit-max constants (e.g. 2^64-1) are preserved.
MBAValue parseInBase(const string &s, int base) {
  MBAValue v(128, 0);
  MBAValue b(128, base);
  for (char c : s) {
    int d;
    if (c >= '0' && c <= '9')
      d = c - '0';
    else if (c >= 'a' && c <= 'f')
      d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      d = c - 'A' + 10;
    else
      continue;
    if (d >= base)
      break;
    v = v * b + MBAValue(128, d);
  }
  return v;
}
} // namespace

// ------------------------------------------------------------------ lexing
char Parser::peek() const {
  if (idx >= expr.size())
    return 0;
  return expr[idx];
}

char Parser::peekNext() const {
  if (idx + 1 >= expr.size())
    return 0;
  return expr[idx + 1];
}

char Parser::get(bool skipSpaces) {
  char c = peek();
  idx += 1;
  if (skipSpaces)
    while (hasSpace())
      skipSpace();
  return c;
}

void Parser::skipSpace() { idx += 1; }

bool Parser::hasSpace() const { return peek() == ' '; }

bool Parser::hasBitwiseNegatedExpression() const { return peek() == '~'; }

bool Parser::hasNegativeExpression() const { return peek() == '-'; }

bool Parser::hasMultiplicator() const {
  return peek() == '*' && peekNext() != '*';
}

bool Parser::hasPower() const { return peek() == '*' && peekNext() == '*'; }

bool Parser::hasLshift() const { return peek() == '<' && peekNext() == '<'; }

bool Parser::hasBinaryConstant() const {
  return peek() == '0' && peekNext() == 'b';
}

bool Parser::hasHexConstant() const { return peek() == '0' && peekNext() == 'x'; }

bool Parser::hasHexDigit() const {
  return hasDecimalDigit() ||
         ((peek() >= 'a' && peek() <= 'f') || (peek() >= 'A' && peek() <= 'F'));
}

bool Parser::hasDecimalDigit() const {
  return peek() >= '0' && peek() <= '9';
}

bool Parser::hasVariable() const { return hasLetter(); }

bool Parser::hasLetter() const {
  char c = peek();
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// -------------------------------------------------------------- constants
MBAValue Parser::getConstant(size_t start, int base) {
  return parseInBase(expr.substr(start, idx - start), base);
}

shared_ptr<Node> Parser::parseConstant() {
  if (hasBinaryConstant())
    return parseBinaryConstant();
  if (hasHexConstant())
    return parseHexConstant();
  return parseDecimalConstant();
}

shared_ptr<Node> Parser::parseBinaryConstant() {
  get(false); // skip '0'
  get(false); // skip 'b'
  if (peek() != '0' && peek() != '1') {
    error_ = "Invalid binary digit near to " + string(1, peek());
    return nullptr;
  }
  size_t start = idx;
  while (peek() == '0' || peek() == '1')
    get(false);
  while (hasSpace())
    skipSpace();
  auto node = newNode(NodeType::CONSTANT);
  node->constant = getConstant(start, 2);
  return node;
}

shared_ptr<Node> Parser::parseHexConstant() {
  get(false); // skip '0'
  get(false); // skip 'x'
  if (!hasHexDigit()) {
    error_ = "Invalid hex digit near to " + string(1, peek());
    return nullptr;
  }
  size_t start = idx;
  while (hasHexDigit())
    get(false);
  while (hasSpace())
    skipSpace();
  auto node = newNode(NodeType::CONSTANT);
  node->constant = getConstant(start, 16);
  return node;
}

shared_ptr<Node> Parser::parseDecimalConstant() {
  if (!hasDecimalDigit()) {
    error_ = "Expecting constant at " + string(1, peek()) + ", but no digit around.";
    return nullptr;
  }
  size_t start = idx;
  while (hasDecimalDigit())
    get(false);
  while (hasSpace())
    skipSpace();
  auto node = newNode(NodeType::CONSTANT);
  node->constant = getConstant(start, 10);
  return node;
}

// -------------------------------------------------------------- variable
shared_ptr<Node> Parser::parseVariable() {
  size_t start = idx;
  get(false); // skip first character (already checked)
  while (hasDecimalDigit() || hasLetter() || peek() == '_')
    get(false);

  if (peek() == '[') {
    get(false);
    while (hasDecimalDigit())
      get(false);
    if (peek() == ']')
      get();
    else
      return nullptr;
  } else {
    while (hasSpace())
      skipSpace();
  }

  auto node = newNode(NodeType::VARIABLE);
  string name = expr.substr(start, idx - start);
  while (!name.empty() && name.back() == ' ')
    name.pop_back(); // rstrip
  node->vname = name;
  return node;
}

// ----------------------------------------------------------- factor / unary
shared_ptr<Node> Parser::multiplyByMinusOne(shared_ptr<Node> node) {
  if (node->type == NodeType::CONSTANT) {
    node->constant = -node->constant;
    return node;
  }
  if (node->type == NodeType::PRODUCT) {
    if (node->children[0]->type == NodeType::CONSTANT) {
      node->children[0]->constant = -node->children[0]->constant;
      return node;
    }
    auto minusOne = newNode(NodeType::CONSTANT);
    minusOne->constant = MBAOps::fromSigned(-1);
    node->children.insert(node->children.begin(), minusOne);
    return node;
  }
  auto minusOne = newNode(NodeType::CONSTANT);
  minusOne->constant = MBAOps::fromSigned(-1);
  auto prod = newNode(NodeType::PRODUCT);
  prod->children = {minusOne, node};
  return prod;
}

shared_ptr<Node> Parser::parseFactor() {
  if (hasBitwiseNegatedExpression())
    return parseBitwiseNegatedExpression();
  if (hasNegativeExpression())
    return parseNegativeExpression();
  return parsePower();
}

shared_ptr<Node> Parser::parseBitwiseNegatedExpression() {
  get(); // skip '~'
  auto node = newNode(NodeType::NEGATION);
  auto child = parseFactor();
  if (child == nullptr)
    return nullptr;
  node->children = {child};
  return node;
}

shared_ptr<Node> Parser::parseNegativeExpression() {
  get(); // skip '-'
  auto node = parseFactor();
  if (node == nullptr)
    return nullptr;
  return multiplyByMinusOne(node);
}

shared_ptr<Node> Parser::parsePower() {
  auto base = parseTerminal();
  if (base == nullptr)
    return nullptr;
  if (!hasPower())
    return base;

  auto node = newNode(NodeType::POWER);
  node->children.push_back(base);

  get(); // skip '*'
  get(); // skip '*'

  auto exp = parseTerminal();
  if (exp == nullptr)
    return nullptr;
  node->children.push_back(exp);

  if (hasPower()) {
    error_ = "Disallowed nested power operator near " + string(1, peek());
    return nullptr;
  }
  return node;
}

shared_ptr<Node> Parser::parseTerminal() {
  if (peek() == '(') {
    get();
    auto node = parseInclusiveDisjunction();
    if (node == nullptr)
      return nullptr;
    if (peek() != ')') {
      error_ = "Missing closing parentheses near to " + string(1, peek());
      return nullptr;
    }
    get();
    return node;
  }

  if (hasVariable())
    return parseVariable();

  return parseConstant();
}

// -------------------------------------------------------- product / sum / shift
shared_ptr<Node> Parser::parseProduct() {
  auto child = parseFactor();
  if (child == nullptr)
    return nullptr;
  if (!hasMultiplicator())
    return child;

  auto node = newNode(NodeType::PRODUCT);
  node->children.push_back(child);

  while (hasMultiplicator()) {
    get();
    child = parseFactor();
    if (child == nullptr)
      return nullptr;
    node->children.push_back(child);
  }
  return node;
}

shared_ptr<Node> Parser::parseSum() {
  auto child = parseProduct();
  if (child == nullptr)
    return nullptr;
  if (peek() != '+' && peek() != '-')
    return child;

  auto node = newNode(NodeType::SUM);
  node->children.push_back(child);

  while (peek() == '+' || peek() == '-') {
    bool negative = peek() == '-';
    get();
    child = parseProduct();
    if (child == nullptr)
      return nullptr;
    if (negative)
      node->children.push_back(multiplyByMinusOne(child));
    else
      node->children.push_back(child);
  }
  return node;
}

shared_ptr<Node> Parser::parseShift() {
  auto base = parseSum();
  if (base == nullptr)
    return nullptr;

  if (!hasLshift())
    return base;

  // We write "a << b" as "a * 2**b".
  auto prod = newNode(NodeType::PRODUCT);
  prod->children.push_back(base);

  get(); // skip '<'
  get(); // skip '<'

  auto op = parseSum();
  if (op == nullptr)
    return nullptr;

  auto power = newNode(NodeType::POWER);
  auto two = newNode(NodeType::CONSTANT);
  two->constant = MBAOps::fromSigned(2);
  power->children.push_back(two);
  power->children.push_back(op);

  prod->children.push_back(power);

  if (hasLshift()) {
    error_ = "Disallowed nested lshift operator near " + string(1, peek());
    return nullptr;
  }
  return prod;
}

// ------------------------------------------------- bitwise (low precedence)
shared_ptr<Node> Parser::parseConjunction() {
  auto child = parseShift();
  if (child == nullptr)
    return nullptr;
  if (peek() != '&')
    return child;

  auto node = newNode(NodeType::CONJUNCTION);
  node->children.push_back(child);

  while (peek() == '&') {
    get();
    child = parseShift();
    if (child == nullptr)
      return nullptr;
    node->children.push_back(child);
  }
  return node;
}

shared_ptr<Node> Parser::parseExclusiveDisjunction() {
  auto child = parseConjunction();
  if (child == nullptr)
    return nullptr;
  if (peek() != '^')
    return child;

  auto node = newNode(NodeType::EXCL_DISJUNCTION);
  node->children.push_back(child);

  while (peek() == '^') {
    get();
    child = parseConjunction();
    if (child == nullptr)
      return nullptr;
    node->children.push_back(child);
  }
  return node;
}

shared_ptr<Node> Parser::parseInclusiveDisjunction() {
  auto child = parseExclusiveDisjunction();
  if (child == nullptr)
    return nullptr;
  if (peek() != '|')
    return child;

  auto node = newNode(NodeType::INCL_DISJUNCTION);
  node->children.push_back(child);

  while (peek() == '|') {
    get();
    child = parseExclusiveDisjunction();
    if (child == nullptr)
      return nullptr;
    node->children.push_back(child);
  }
  return node;
}

// ------------------------------------------------------------------ entry
shared_ptr<Node> Parser::parseExpression() {
  if (hasSpace())
    skipSpace();
  auto root = parseInclusiveDisjunction();

  while (idx < expr.size() && hasSpace())
    skipSpace();

  if (idx != expr.size()) {
    error_ = "Finished near to " + string(1, peek()) + " before everything was parsed";
    return nullptr;
  }
  return root;
}

// Mirrors parse.py parse().
shared_ptr<Node>
parse(const string &expr, int bitCount, bool reduceConstants, bool refine,
      bool markLinear) {
  Parser parser(expr, bitCount, reduceConstants);
  auto root = parser.parseExpression();

  // refine() / markLinear() are provided by later phases (Node rewrite rules /
  // mark_linear). Until then these flags are accepted but not applied.
  (void)refine;
  (void)markLinear;

  return root;
}

} // namespace MBA
} // namespace LSiMBA
