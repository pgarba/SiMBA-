#ifndef MBACHECKER_H
#define MBACHECKER_H

#include <string>

namespace LSiMBA {

enum ExpressionType { bitwise = 0, arithmetic = 1, mixed = 2 };

class MBAChecker {
public:
  MBAChecker();

  bool check_expression(const std::string &expression);

  bool check_inclusive_disjunction(ExpressionType &ExprType);

  bool check_exclusive_disjunction(ExpressionType &ExprType);

  bool check_conjunction(ExpressionType &ExprType);

  bool check_sum(ExpressionType &ExprType);

  bool check_product(ExpressionType &ExprType);

  bool check_factor(ExpressionType &ExprType);

  bool check_bitwise_negated_expression(ExpressionType &ExprType);

  bool check_negative_expression(ExpressionType &ExprType);

  bool check_terminal(ExpressionType &ExprType);

  bool check_variable();

  bool check_constant();

  bool check_binary_constant();

  bool check_hex_constant();

  bool check_decimal_constant();

  char get();

  void skip_space();

  char peek();

  bool has_bitwise_negated_expression();

  bool has_negative_expression();

  bool has_binary_constant();

  bool has_hex_constant();

  bool has_hex_digit();

  bool has_decimal_digit();

  bool has_variable();

  bool has_letter();

  bool has_space();

  char peek_next();

private:
  int idx;

  std::string expr;
};

} // namespace LSiMBA

#endif // MBACHECKER_H