#include <iostream>
#include <math.h>
#include <sstream>
#include <stdio.h>
#include <string>
#include <tuple>
#include <vector>

#include <llvm/ADT/APInt.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>

#include "veque.h"

#define Use_APINT true

using namespace llvm;

class Token {
public:
  enum class Type {
    Unknown,
    Number,
    Operator,
    LeftParen,
    RightParen,
  };

  Token(Type type, const std::string &s, int precedence = -1,
        bool rightAssociative = false, bool unary = false)
      : type{type}, str(s), precedence{precedence},
        rightAssociative{rightAssociative}, unary{unary} {}

  Type type;
  std::string str;
  int precedence;
  bool rightAssociative;
  bool unary;
};

void exprToTokens(const std::string &expr, veque::veque<Token> &tokens) {
  for (const auto *p = expr.c_str(); *p; ++p) {
    if (isblank(*p)) {
      // do nothing
    } else if (isdigit(*p)) {
      const auto *b = p;
      while (isdigit(*p)) {
        ++p;
      }
      const auto s = std::string(b, p);
      tokens.push_back(Token{Token::Type::Number, s});
      --p;
    } else {
      Token::Type t = Token::Type::Unknown;
      int precedence = -1;
      bool rightAssociative = false;
      bool unary = false;
      char c = *p;
      switch (c) {
      default:
        break;
      case '(':
        t = Token::Type::LeftParen;
        break;
      case ')':
        t = Token::Type::RightParen;
        break;
      case '*':
        t = Token::Type::Operator;
        precedence = 6;
        break;
      case '/':
        t = Token::Type::Operator;
        precedence = 6;
        break;
      case '&':
        t = Token::Type::Operator;
        precedence = 4;
        break;
      case '|':
        t = Token::Type::Operator;
        precedence = 2;
        break;
      case '^':
        t = Token::Type::Operator;
        precedence = 3;
        break;
      case '+':
        t = Token::Type::Operator;
        precedence = 5;
        break;
      case '~':
        unary = true;
        t = Token::Type::Operator;
        // Increase precendence if last token was unary and ~,! or -
        if (!tokens.empty() && tokens.back().unary) {
          // Calc this one first
          precedence = tokens.back().precedence + 1;
        } else {
          // Keep default
          precedence = 7;
        }
        break;
      case '!':
        unary = true;
        t = Token::Type::Operator;
        // Increase precendence if last token was unary and ~,! or -
        if (!tokens.empty() && tokens.back().unary) {
          // Calc this one first
          precedence = tokens.back().precedence + 1;
        } else {
          // Keep default
          precedence = 7;
        }
        break;
      case '-':
        // If current token is '-'
        // and if it is the first token, or preceded by another operator, or
        // left-paren,
        if (tokens.empty() || tokens.back().type == Token::Type::Operator ||
            tokens.back().type == Token::Type::LeftParen) {
          // it's unary '-'
          // note#1 : 'm' is a special operator name for unary '-'
          // note#2 : It has highest precedence than any of the infix operators
          unary = true;
          c = 'm';
          t = Token::Type::Operator;

          // Increase precendence if last token was unary and ~,! or -
          if (!tokens.empty() && tokens.back().unary) {
            // Calc this one first
            precedence = tokens.back().precedence + 1;
          } else {
            // Keep default
            precedence = 7;
          }
        } else {
          // otherwise, it's binary '-'
          t = Token::Type::Operator;
          precedence = 5;
        }
        break;
      }
      const auto s = std::string(1, c);
      tokens.push_back(Token{t, s, precedence, rightAssociative, unary});
    }
  }
}

void shuntingYard(const veque::veque<Token> &tokens,
                  veque::veque<Token> &queue) {
  SmallVector<Token, 32> stack;

  // While there are tokens to be read:
  for (auto token : tokens) {
    // Read a token
    switch (token.type) {
    case Token::Type::Number:
      // If the token is a number, then add it to the output queue
      queue.push_back(token);
      break;

    case Token::Type::Operator: {
      // If the token is operator, o1, then:
      const auto o1 = token;

      // while there is an operator token,
      while (!stack.empty()) {
        // o2, at the top of stack, and
        const auto o2 = stack.back();

        // either o1 is left-associative and its precedence is
        // *less than or equal* to that of o2,
        // or o1 if right associative, and has precedence
        // *less than* that of o2,
        if ((!o1.rightAssociative && o1.precedence <= o2.precedence) ||
            (o1.rightAssociative && o1.precedence < o2.precedence)) {
          // then pop o2 off the stack,
          stack.pop_back();
          // onto the output queue;
          queue.push_back(o2);

          continue;
        }

        // @@ otherwise, exit.
        break;
      }

      // push o1 onto the stack.
      stack.push_back(o1);
    } break;

    case Token::Type::LeftParen:
      // If token is left parenthesis, then push it onto the stack
      stack.push_back(token);
      break;

    case Token::Type::RightParen:
      // If token is right parenthesis:
      {
        bool match = false;

        // Until the token at the top of the stack
        // is a left parenthesis,
        while (!stack.empty() && stack.back().type != Token::Type::LeftParen) {
          // pop operators off the stack
          // onto the output queue.
          queue.push_back(stack.back());
          stack.pop_back();
          match = true;
        }

        if (!match && stack.empty()) {
          // If the stack runs out without finding a left parenthesis,
          // then there are mismatched parentheses.
          printf("RightParen error (%s)\n", token.str.c_str());
          return;
        }

        // Pop the left parenthesis from the stack,
        // but not onto the output queue.
        stack.pop_back();
      }
      break;

    default:
      printf("error (%s)\n", token.str.c_str());
      return;
    }
  }

  // When there are no more tokens to read:
  //   While there are still operator tokens in the stack:
  while (!stack.empty()) {
    // If the operator token on the top of the stack is a parenthesis,
    // then there are mismatched parentheses.
    if (stack.back().type == Token::Type::LeftParen) {
      printf("Mismatched parentheses error\n");
      return;
    }

    // Pop the operator onto the output queue.
    queue.push_back(std::move(stack.back()));
    stack.pop_back();
  }
}

void replace_all(std::string &str, const std::string &from,
                             const std::string &to) {
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length();
  }
}

int64_t eval(std::string expr, SmallVector<int64_t, 16> &par) {
  // Replace variables with values in expression
  for (int i = 0; i < par.size(); i++) {
    std::string var = "X[" + std::to_string(i) + "]";
    std::string val = std::to_string(par[i]);

    replace_all(expr, var, val);
  }
  
  veque::veque<Token> tokens;

  exprToTokens(expr, tokens);
  
  veque::veque<Token> queue;
  shuntingYard(tokens, queue);

  SmallVector<APInt, 32> stackAP;

  while (!queue.empty()) {
    const auto token = queue.front();
    queue.pop_front();
    switch (token.type) {
    case Token::Type::Number: {
      APInt APV(128, token.str, 10);
      stackAP.push_back(APV);

    } break;

    case Token::Type::Operator: {
      if (token.unary) {
        // unray operators
        const auto rhsAP = stackAP.back();
        stackAP.pop_back();

        switch (token.str[0]) {
        default:
          printf("Operator error [%s]\n", token.str.c_str());
          exit(0);
          break;
        case 'm': // Special operator name for unary '-'
          stackAP.push_back(-rhsAP);

          break;
        case '~':
          stackAP.push_back(~rhsAP);

          break;
        case '!':
#ifdef Use_APINT
#else
          stack.push_back(!rhs);
#endif
          // stackAP.push_back(rhsAP);
          printf("! operator not implemented\n");
          exit(-1);
          break;
        }
      } else {
        // binary operators
        const auto rhsAP = stackAP.back();
        stackAP.pop_back();

        const auto lhsAP = stackAP.back();
        stackAP.pop_back();

        switch (token.str[0]) {
        default:
          printf("Operator error [%s]\n", token.str.c_str());
          exit(0);
          break;
        case '^':
          stackAP.push_back(lhsAP ^ rhsAP);

          break;
        case '*':
          stackAP.push_back(lhsAP * rhsAP);

          break;
        case '/':
          stackAP.push_back(lhsAP.sdiv(rhsAP));

          break;
        case '&':
          stackAP.push_back(lhsAP & rhsAP);

          break;
        case '|':
          stackAP.push_back(lhsAP | rhsAP);

          break;
        case '+':
          stackAP.push_back(lhsAP + rhsAP);

          break;
        case '-':
          stackAP.push_back(lhsAP - rhsAP);

          break;
        }
      }
    } break;

    default:
      printf("Token error\n");
      exit(0);
    }
  }

  // Dont trigger the assert in LLVM
  APInt APn = stackAP.back() & 0xFFFFFFFFFFFFFFFF;

  return APn.getSExtValue();
}
