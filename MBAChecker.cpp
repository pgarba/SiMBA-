#include "MBAChecker.h"

namespace LSiMBA {

MBAChecker::MBAChecker() { this->idx = 0; }

bool MBAChecker::check_expression(const std::string &expression) {
  this->expr = expression;
  this->idx = 0;

  ExpressionType Type;
  auto valid = this->check_inclusive_disjunction(Type);
  if (!valid)
    return false;

  while (this->idx < this->expr.size() && this->has_space()) {
    this->skip_space();
  }

  return this->idx == this->expr.size();
}

bool MBAChecker::check_inclusive_disjunction(ExpressionType &ExprType) {
  auto valid = this->check_exclusive_disjunction(ExprType);
  if (!valid) {
    return false;
  }

  if (ExprType == ExpressionType::mixed && this->peek() == '|') {
    return false;
  }

  while (this->peek() == '|') {
    this->get();

    ExpressionType t;
    auto valid = this->check_exclusive_disjunction(t);
    if (!valid || t != ExprType) {
      return false;
    }
  }

  return true;
}

bool MBAChecker::check_exclusive_disjunction(ExpressionType &ExprType) {
  auto valid = this->check_conjunction(ExprType);
  if (!valid) {
    return false;
  }

  if (ExprType == ExpressionType::mixed && this->peek() == '^') {
    return false;
  }

  while (this->peek() == '^') {
    this->get();

    ExpressionType t;
    auto valid = this->check_conjunction(t);
    if (!valid || t != ExprType) {
      return false;
    }
  }

  return true;
}

bool MBAChecker::check_conjunction(ExpressionType &ExprType) {
  auto valid = this->check_sum(ExprType);
  if (!valid) {
    return false;
  }

  if (ExprType == ExpressionType::mixed && this->peek() == '&') {
    return false;
  }

  while (this->peek() == '&') {
    this->get();

    ExpressionType t;
    auto valid = this->check_sum(t);
    if (!valid || t != ExprType) {
      return false;
    }
  }

  return true;
}

bool MBAChecker::check_sum(ExpressionType &ExprType) {
  auto valid = this->check_product(ExprType);
  if (!valid) {
    return false;
  }

  while (this->peek() == '+' || this->peek() == '-') {
    this->get();

    ExpressionType t;
    auto valid = this->check_product(t);
    if (!valid) {
      return false;
    }

    if (ExprType == ExpressionType::bitwise ||
        (ExprType != ExpressionType::mixed && t != ExprType)) {
      ExprType = ExpressionType::mixed;
    }
  }

  return true;
}

bool MBAChecker::check_product(ExpressionType &ExprType) {
  auto valid = this->check_factor(ExprType);
  if (!valid) {
    return false;
  }

  int bitwiseCount = (ExprType != 1);

  while (this->peek() == '*') {
    this->get();

    ExpressionType t;
    auto valid = this->check_factor(t);
    if (!valid) {
      return false;
    }

    if (t != ExpressionType::arithmetic) {
      bitwiseCount += 1;
    }

    if (ExprType != ExpressionType::mixed && t != ExprType) {
      ExprType = ExpressionType::mixed;
    }

    if (bitwiseCount > 1) {
      return false;
    }
  }

  return true;
}

bool MBAChecker::check_factor(ExpressionType &ExprType) {
  if (this->peek() == '(') {
    this->get();

    auto valid = check_inclusive_disjunction(ExprType);
    if (!valid) {
      return false;
    }

    if (this->peek() != ')') {
      return false;
    }
    this->get();

    return true;
  }

  if (this->has_bitwise_negated_expression()) {
    return check_bitwise_negated_expression(ExprType);
  }

  if (this->has_negative_expression()) {
    return check_negative_expression(ExprType);
  }

  return this->check_terminal(ExprType);
}

bool MBAChecker::check_bitwise_negated_expression(ExpressionType &ExprType) {
  this->get();

  while (this->has_bitwise_negated_expression()) {
    this->get();
  }

  if (this->peek() == '(') {
    this->get();
    auto valid = this->check_inclusive_disjunction(ExprType);
    if (!valid || ExprType == ExpressionType::mixed) {
      return false;
    }
    if (this->peek() != ')') {
      return false;
    }
    this->get();

    return true;
  }

  return this->check_terminal(ExprType);
}

bool MBAChecker::check_negative_expression(ExpressionType &ExprType) {
  this->get();

  while (this->has_negative_expression()) {
    this->get();
  }

  if (this->peek() == '(') {
    this->get();
    auto valid = this->check_inclusive_disjunction(ExprType);
    if (!valid) {
      return false;
    }
    if (this->peek() != ')') {
      return false;
    }
    this->get();

    if (ExprType == ExpressionType::bitwise) {
      ExprType = ExpressionType::mixed;
    }

    return true;
  }

  if (this->has_bitwise_negated_expression()) {
    return this->check_bitwise_negated_expression(ExprType);
  }

  auto valid = this->check_terminal(ExprType);
  if (ExprType == ExpressionType::bitwise) {
    ExprType = ExpressionType::mixed;
  }

  return true;
}

bool MBAChecker::check_terminal(ExpressionType &ExprType) {
  if (this->has_variable()) {
    auto Valid = this->check_variable();
    ExprType = ExpressionType::bitwise;
    return Valid;
  }

  auto valid = this->check_constant();
  if (!valid) {
    ExprType = ExpressionType::arithmetic;
    return false;
  }

  if (this->peek() == '*' && this->peek_next() == '*') {
    this->get();
    this->get();

    if (this->peek() == '(') {
      this->get();
      auto valid = this->check_inclusive_disjunction(ExprType);
      if (ExprType != ExpressionType::arithmetic) {
        return false;
      }
      if (!valid) {
        ExprType = ExpressionType::arithmetic;
        return false;
      }
      if (this->peek() != ')') {
        ExprType = ExpressionType::arithmetic;
        return false;
      }
      this->get();
      ExprType = ExpressionType::arithmetic;
      return true;
    }

    auto valid = this->check_constant();
    ExprType = ExpressionType::arithmetic;
    return true;
  }

  ExprType = ExpressionType::arithmetic;
  return true;
}

bool MBAChecker::check_variable() {
  this->get();

  while (this->has_decimal_digit() || this->has_letter() ||
         this->peek() == '_') {
    this->get();
  }

  return true;
}

bool MBAChecker::check_constant() {
  if (this->has_binary_constant()) {
    return this->check_binary_constant();
  }

  if (this->has_hex_constant()) {
    return this->check_hex_constant();
  }

  return this->check_decimal_constant();
}

bool MBAChecker::check_binary_constant() {
  // Skip '0' and 'b'.
  this->get();
  this->get();

  if (this->peek() != '0' && this->peek() != '1') {
    return false;
  }

  while (this->peek() == '0' || this->peek() == '1') {
    this->get();
  }

  return true;
}

bool MBAChecker::check_hex_constant() {
  this->get();
  this->get();

  if (!this->has_hex_digit()) {
    return false;
  }

  while (this->has_hex_digit()) {
    this->get();
  }

  return true;
}

bool MBAChecker::check_decimal_constant() {
  if (!this->has_decimal_digit()) {
    return false;
  }

  while (this->has_decimal_digit()) {
    this->get();
  }

  return true;
}

char MBAChecker::get() {
  char c = this->peek();
  this->idx += 1;

  while (this->has_space()) {
    this->skip_space();
  }

  return c;
}

void MBAChecker::skip_space() { this->idx += 1; }

char MBAChecker::peek() {
  if (this->idx >= this->expr.size()) {
    return '\0';
  }

  return this->expr.at(this->idx);
}

bool MBAChecker::has_bitwise_negated_expression() {
  return this->peek() == '~';
}

bool MBAChecker::has_negative_expression() { return this->peek() == '-'; }

bool MBAChecker::has_binary_constant() {
  return this->peek() == '0' && this->peek_next() == 'b';
}

bool MBAChecker::has_hex_constant() {
  return this->peek() == '0' && this->peek_next() == 'x';
}

bool MBAChecker::has_hex_digit() {
  auto c = this->peek();
  if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
    return true;
  }

  return false;
}

bool MBAChecker::has_decimal_digit() {
  auto c = this->peek();
  if (c >= '0' && c <= '9') {
    return true;
  }

  return false;
}

bool MBAChecker::has_variable() { return this->has_letter(); }

bool MBAChecker::has_letter() {
  auto c = this->peek();
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
    return true;
  }

  return false;
}

bool MBAChecker::has_space() { return this->peek() == ' '; }

char MBAChecker::peek_next() {
  if (this->idx >= (this->expr.size() - 1))
    return '\0';

  return this->expr.at(this->idx + 1);
}

}; // namespace LSiMBA