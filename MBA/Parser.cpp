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

bool Parser::hasRshift() const { return peek() == '>' && peekNext() == '>'; }

bool Parser::hasDiv() const { return peek() == '/'; }

bool Parser::hasRem() const { return peek() == '%'; }

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
// Handles the multiplicative-level operators '*', '/' and '%'
// left-associatively. '/' and '%' desugar to bit terms (the LHS must be a full
// variable and the RHS a constant power of two).
shared_ptr<Node> Parser::parseProduct() {
  auto acc = parseFactor();
  if (acc == nullptr)
    return nullptr;

  if (!hasMultiplicator() && !hasDiv() && !hasRem())
    return acc;

  while (hasMultiplicator() || hasDiv() || hasRem()) {
    if (hasMultiplicator()) {
      get();
      auto child = parseFactor();
      if (child == nullptr)
        return nullptr;
      if (acc->type == NodeType::PRODUCT) {
        acc->children.push_back(child);
      } else {
        auto node = newNode(NodeType::PRODUCT);
        node->children.push_back(acc);
        node->children.push_back(child);
        acc = node;
      }
    } else {
      std::string kind = hasDiv() ? "/" : "%";
      get();
      auto child = parseFactor();
      if (child == nullptr)
        return nullptr;
      acc = desugarDivRem(acc, child, kind);
      if (acc == nullptr)
        return nullptr;
    }
  }
  return acc;
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

  if (!hasLshift() && !hasRshift())
    return base;

  std::shared_ptr<Node> result;
  if (hasLshift()) {
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
    result = prod;
  } else {
    // "a >> b" (b a non-negative constant shift amount).
    get(); // skip '>'
    get(); // skip '>'

    auto op = parseSum();
    if (op == nullptr)
      return nullptr;

    result = desugarDivRem(base, op, ">>");
    if (result == nullptr)
      return nullptr;
  }

  // Nested shift operations require parentheses.
  if (hasLshift() || hasRshift()) {
    error_ = "Disallowed nested shift operator near " + string(1, peek());
    return nullptr;
  }
  return result;
}

// ---------------------------------------------------------------- desugar
bool Parser::isFullVariable(const Node &n) const {
  return n.type == NodeType::VARIABLE && n.vname.find('[') == std::string::npos;
}

int Parser::pow2Exponent(const MBAValue &v) const {
  // Truncate to 64 bits first: getZExtValue() asserts on values with more
  // than 64 active bits (e.g. a sign-extended negative constant).
  std::uint64_t low = v.zextOrTrunc(64).getZExtValue();
  std::uint64_t high = v.lshr(64).zextOrTrunc(64).getZExtValue();
  if (high != 0)
    return -1; // negative or larger than 2^63
  if (low == 0)
    return -1; // 0 is not a power of two
  if ((low & (low - 1)) != 0)
    return -1; // not a power of two
  return MBAOps::trailingZeros(low);
}

std::shared_ptr<Node> Parser::bitTerm(const std::string &name, int i,
                                     int shift) {
  auto var = newNode(NodeType::VARIABLE);
  var->vname = name + "[" + std::to_string(i) + "]";
  if (shift == 0)
    return var;
  // Coefficient 2**shift as a single constant so the term a[i] * const is
  // recognized as linear by the simplifier. A 2**shift POWER node would make
  // the whole desugared expression nonlinear and push the general simplifier
  // into its slow 2**vnumber enumeration path (hang/OOM for wide values).
  auto coeff = newNode(NodeType::CONSTANT);
  coeff->constant = MBAOps::pow2(shift);
  auto prod = newNode(NodeType::PRODUCT);
  prod->children.push_back(var);
  prod->children.push_back(coeff);
  return prod;
}

std::shared_ptr<Node>
Parser::desugarDivRem(std::shared_ptr<Node> base, std::shared_ptr<Node> op,
                      const std::string &kind) {
  int B = bitCount;
  if (op == nullptr)
    return nullptr;

  // --- Tier 1: bit desugar when the LHS is a full variable and the RHS is a
  // constant (power-of-two for / and %). ---
  if (isFullVariable(*base) && op->type == NodeType::CONSTANT) {
    // Truncate to 64 bits first: getZExtValue() asserts on values with more
    // than 64 active bits (e.g. a sign-extended negative constant).
    std::uint64_t low = op->constant.zextOrTrunc(64).getZExtValue();
    std::uint64_t high = op->constant.lshr(64).zextOrTrunc(64).getZExtValue();
    if (high == 0) {
      std::uint64_t k;
      bool pow2ok = true;
      if (kind == ">>") {
        k = low; // shift amount, any non-negative integer
      } else {
        // "/" or "%": the divisor must be a power of two.
        int e = pow2Exponent(op->constant);
        if (e < 0)
          pow2ok = false;
        k = static_cast<std::uint64_t>(e);
      }
      if (pow2ok) {
        std::string name = base->vname;
        std::vector<std::shared_ptr<Node>> terms;
        if (kind == "%") {
          if (k == 0) {
            auto zero = newNode(NodeType::CONSTANT);
            zero->constant = MBAOps::fromSigned(0);
            return zero;
          }
          if (k >= static_cast<std::uint64_t>(B))
            return base; // base % 2**B == base
          for (std::uint64_t i = 0; i < k; ++i)
            terms.push_back(bitTerm(name, static_cast<int>(i), static_cast<int>(i)));
        } else { // ">>" or "/"
          if (k == 0)
            return base; // base >> 0 == base
          if (k >= static_cast<std::uint64_t>(B)) {
            auto zero = newNode(NodeType::CONSTANT);
            zero->constant = MBAOps::fromSigned(0);
            return zero;
          }
          for (std::uint64_t i = k; i < static_cast<std::uint64_t>(B); ++i)
            terms.push_back(
                bitTerm(name, static_cast<int>(i), static_cast<int>(i - k)));
        }

        if (terms.size() == 1)
          return terms[0];
        auto sum = newNode(NodeType::SUM);
        sum->children = std::move(terms);
        return sum;
      }
    }
  }

  // --- Tier 2: first-class operator node (exact unsigned semantics, mod 2^B).
  // Used when the Tier 1 desugar does not apply (non-variable LHS, non-constant
  // or non-power-of-two RHS, etc.). ---
  if ((kind == "/" || kind == "%") && op->type == NodeType::CONSTANT &&
      op->constant == MBAOps::fromSigned(0)) {
    error_ = "Division/remainder by zero is not supported";
    return nullptr;
  }
  NodeType opType =
      (kind == ">>") ? NodeType::RSHIFT : (kind == "/" ? NodeType::UDIV : NodeType::UREM);
  auto node = newNode(opType);
  node->children.push_back(base);
  node->children.push_back(op);
  return node;
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
