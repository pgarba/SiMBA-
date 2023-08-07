#include <iostream>
#include <map>
#include <math.h>
#include <sstream>
#include <stdio.h>
#include <string>
#include <tuple>
#include <vector>

#include <llvm/ADT/APInt.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>

#include <z3++.h>

#include "veque.h"

using namespace llvm;

class Token {
public:
  enum class Type {
    Unknown,
    Number,
    Operator,
    LeftParen,
    RightParen,
    Variable
  };

  Token(Type type, const std::string &s, int precedence = -1,
        bool rightAssociative = false, bool unary = false)
      : type{type}, str(s), precedence{precedence},
        rightAssociative{rightAssociative}, unary{unary}, ArgIndex{0} {}

  Type type;

  std::string str;

  int precedence;

  bool rightAssociative;

  bool unary;

  // Arg Index if Variable type
  int ArgIndex;
};

int8_t isVariable(const char *c, std::vector<std::string> *VNames) {
  if (VNames == nullptr)
    report_fatal_error("VNames is nullptr");

  for (int i = 0; i < VNames->size(); i++) {
    if (strncmp(c, (*VNames)[i].c_str(), (*VNames)[i].size()) == 0)
      return i;
  }

  return -1;
}

int countOperators(std::string &expr) {
  int OpCount = 0;
  for (const auto *p = expr.c_str(); *p; ++p) {
    switch (*p) {
    case '*':
      OpCount++;
      break;
    case '/':
      OpCount++;
      break;
    case '&':
      OpCount++;
      break;
    case '|':
      OpCount++;
      break;
    case '^':
      OpCount++;
      break;
    case '+':
      OpCount++;
      break;
    case '~':
      OpCount++;
      break;
    case '!':
      OpCount++;
      break;
    case '-':
      OpCount++;
      break;
    default:
      break;
    }
  }
  return OpCount;
}

void exprToTokens(const std::string &expr, veque::veque<Token> &tokens,
                  bool detectVariables = false,
                  std::vector<std::string> *VNames = nullptr) {
  int Index;
  for (const auto *p = expr.c_str(); *p; ++p) {
    if (isblank(*p)) {
      // do nothing
    } else if (detectVariables && (Index = isVariable(p, VNames)) != -1) {
      // detected a variable
      auto t = Token{Token::Type::Variable, (*VNames)[Index]};
      t.ArgIndex = Index;
      tokens.push_back(t);

      // skip the variable name
      p += (*VNames)[Index].size() - 1;
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
        llvm::outs() << "[exprToTokens]: Unkown Token '" << c << "'\n";
        report_fatal_error("", false);
        break;
      case '(':
        t = Token::Type::LeftParen;
        break;
      case ')':
        t = Token::Type::RightParen;
        break;
      case '*':
        t = Token::Type::Operator;
        precedence = 7;
        break;
      case '>':
        t = Token::Type::Operator;
        precedence = 5;
        break;
      case '#':
        t = Token::Type::Operator;
        precedence = 8;
        break;
      case '/':
        t = Token::Type::Operator;
        precedence = 7;
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
        precedence = 6;
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
          precedence = 9;
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
          precedence = 9;
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
            precedence = 9;
          }
        } else {
          // otherwise, it's binary '-'
          t = Token::Type::Operator;
          precedence = 6;
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
    case Token::Type::Variable: {
      queue.push_back(token);
    } break;

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

void createLLVMReplacement(llvm::Instruction *InsertionPoint,
                           llvm::Type *IntType, std::string &expr,
                           std::vector<std::string> &VNames,
                           llvm::SmallVectorImpl<llvm::Value *> &Variables) {

  // replace ** with p (pow operator is supported by GAMBA)
  replace_all(expr, "**", "#");

  // Parse expressions and create token
  veque::veque<Token> tokens;
  exprToTokens(expr, tokens, true, &VNames);

  // Create deque to parse the operations
  veque::veque<Token> queue;
  shuntingYard(tokens, queue);

  // Create the builder to build
  llvm::IRBuilder<> Builder(InsertionPoint);

  SmallVector<llvm::Value *, 32> stackAP;

  while (!queue.empty()) {
    const auto token = queue.front();
    queue.pop_front();
    switch (token.type) {
    case Token::Type::Variable: {
      auto ArgIndex = token.ArgIndex;

      // Cast variable if needed
      auto Var = Variables[ArgIndex];
      if (Var->getType() != IntType) {
        Var = Builder.CreateIntCast(Variables[ArgIndex], IntType, false,
                                    "CastedVar");
      }

      stackAP.push_back(Var);
    } break;
    case Token::Type::Number: {
      APInt APV(IntType->getIntegerBitWidth(), token.str, 10);
      auto CI = llvm::ConstantInt::get(IntType, APV);
      stackAP.push_back(CI);
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
          // stackAP.push_back(-rhsAP);
          stackAP.push_back(Builder.CreateNeg(rhsAP));
          break;
        case '~':
          // stackAP.push_back(~rhsAP);
          stackAP.push_back(Builder.CreateNot(rhsAP));
          break;
        case '!':
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
          // stackAP.push_back(lhsAP ^ rhsAP);
          stackAP.push_back(Builder.CreateXor(lhsAP, rhsAP));
          break;
        case '*':
          // stackAP.push_back(lhsAP * rhsAP);
          stackAP.push_back(Builder.CreateMul(lhsAP, rhsAP));
          break;
        case '>':
          // stackAP.push_back(lhsAP >> rhsAP);
          stackAP.push_back(Builder.CreateAShr(lhsAP, rhsAP));
          break;
        case '#':
          // stackAP.push_back(lhsAP ** rhsAP);
          // Call Pow intrinsics
          {
            auto DblTy = Type::getDoubleTy(lhsAP->getContext());
            auto DlhsAP = Builder.CreateUIToFP(lhsAP, DblTy);
            auto DrhsAP = Builder.CreateUIToFP(rhsAP, DblTy);
            auto Res = Builder.CreateIntrinsic(
                llvm::Intrinsic::pow, {DblTy, DblTy}, {DlhsAP, DrhsAP});
            auto ResInt = Builder.CreateFPToUI(Res, lhsAP->getType());

            stackAP.push_back(ResInt);
          }
          break;
        case '/':
          // stackAP.push_back(lhsAP.sdiv(rhsAP));
          stackAP.push_back(Builder.CreateSDiv(lhsAP, rhsAP));
          break;
        case '&':
          // stackAP.push_back(lhsAP & rhsAP);
          stackAP.push_back(Builder.CreateAnd(lhsAP, rhsAP));
          break;
        case '|':
          // stackAP.push_back(lhsAP | rhsAP);
          stackAP.push_back(Builder.CreateOr(lhsAP, rhsAP));
          break;
        case '+':
          // stackAP.push_back(lhsAP + rhsAP);
          stackAP.push_back(Builder.CreateAdd(lhsAP, rhsAP));
          break;
        case '-':
          // stackAP.push_back(lhsAP - rhsAP);
          stackAP.push_back(Builder.CreateSub(lhsAP, rhsAP));
          break;
        }
      }
    } break;

    default:
      printf("Token error\n");
      exit(0);
    }
  }

  // Replace MBA
  auto &ModV = stackAP.back();

  InsertionPoint->replaceAllUsesWith(ModV);
}

llvm::Function *createLLVMFunction(llvm::Module *M, llvm::Type *IntType,
                                   std::string &expr,
                                   std::vector<std::string> &VNames) {
  // Parse expressions and create token
  veque::veque<Token> tokens;
  exprToTokens(expr, tokens, true, &VNames);

  // Create deque to parse the operations
  veque::veque<Token> queue;
  shuntingYard(tokens, queue);

  // Create new function
  std::vector<llvm::Type *> ArgsTy;
  for (int i = 0; i < VNames.size(); i++) {
    ArgsTy.push_back(IntType);
  }

  auto FTy = llvm::FunctionType::get(IntType, ArgsTy, false);
  auto F = llvm::Function::Create(
      FTy, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "MBA_Simp", *M);

  // Create new BB
  auto *BB = llvm::BasicBlock::Create(M->getContext(), "MBA_BB", F);

  // Create the builder to build
  llvm::IRBuilder<> Builder(BB);

  SmallVector<llvm::Value *, 32> stackAP;

  while (!queue.empty()) {
    const auto token = queue.front();
    queue.pop_front();
    switch (token.type) {
    case Token::Type::Variable: {
      auto ArgIndex = token.ArgIndex;
      stackAP.push_back(F->getArg(ArgIndex));
    } break;
    case Token::Type::Number: {
      APInt APV(IntType->getIntegerBitWidth(), token.str, 10);
      auto CI = llvm::ConstantInt::get(IntType, APV);
      stackAP.push_back(CI);
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
          // stackAP.push_back(-rhsAP);
          stackAP.push_back(Builder.CreateNeg(rhsAP));
          break;
        case '~':
          // stackAP.push_back(~rhsAP);
          stackAP.push_back(Builder.CreateNot(rhsAP));
          break;
        case '!':
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
          // stackAP.push_back(lhsAP ^ rhsAP);
          stackAP.push_back(Builder.CreateXor(lhsAP, rhsAP));
          break;
        case '*':
          // stackAP.push_back(lhsAP * rhsAP);
          stackAP.push_back(Builder.CreateMul(lhsAP, rhsAP));
          break;
        case '/':
          // stackAP.push_back(lhsAP.sdiv(rhsAP));
          stackAP.push_back(Builder.CreateSDiv(lhsAP, rhsAP));
          break;
        case '&':
          // stackAP.push_back(lhsAP & rhsAP);
          stackAP.push_back(Builder.CreateAnd(lhsAP, rhsAP));
          break;
        case '|':
          // stackAP.push_back(lhsAP | rhsAP);
          stackAP.push_back(Builder.CreateOr(lhsAP, rhsAP));
          break;
        case '+':
          // stackAP.push_back(lhsAP + rhsAP);
          stackAP.push_back(Builder.CreateAdd(lhsAP, rhsAP));
          break;
        case '-':
          // stackAP.push_back(lhsAP - rhsAP);
          stackAP.push_back(Builder.CreateSub(lhsAP, rhsAP));
          break;
        }
      }
    } break;

    default:
      printf("Token error\n");
      exit(0);
    }
  }

  // Create return value
  auto &ModV = stackAP.back();

  Builder.CreateRet(ModV);

  return F;
}

APInt APIntPow(APInt base, APInt exp) {
  APInt result(base.getBitWidth(), 1);
  for (;;) {
    if ((exp & 1) == 1)
      result *= base;
    exp = exp.lshr(1);
    if (!exp)
      break;
    base *= base;
  }

  return result;
}

APInt eval(std::string expr, llvm::SmallVectorImpl<APInt> &par, int BitWidth,
           int *OperationCount) {
  // Replace variables with values in expression
  for (int i = 0; i < par.size(); i++) {
    std::string var = "X[" + std::to_string(i) + "]";
    std::string val = std::to_string(par[i].getZExtValue());

    replace_all(expr, var, val);
  }

  // replace ** with p (pow operator is supported by GAMBA)
  replace_all(expr, "**", "#");

  veque::veque<Token> tokens;
  exprToTokens(expr, tokens);

  veque::veque<Token> queue;
  shuntingYard(tokens, queue);

  SmallVector<APInt, 32> stackAP;

  int Operations = 0;

  while (!queue.empty()) {
    const auto token = queue.front();
    queue.pop_front();
    switch (token.type) {
    case Token::Type::Number: {
      APInt APV(BitWidth, token.str, 10);
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
          // stackAP.push_back(rhsAP);
          printf("! operator not implemented\n");
          exit(-1);
          break;
        }
      } else {
        Operations++;

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
        case '>':
          stackAP.push_back(lhsAP.lshr(rhsAP));
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
        case '#':
          stackAP.push_back(APIntPow(lhsAP, rhsAP));
          break;
        }
      }
    } break;

    default:
      printf("Token error\n");
      exit(0);
    }
  }

  // Set operation count
  if (OperationCount) {
    *OperationCount = Operations;
  }

  return stackAP.back();
}

z3::expr getZ3ExprFromString(z3::context &Z3Ctx, std::string &expr,
                             int BitWidth, std::vector<std::string> &Variables,
                             std::map<std::string, llvm::Type *> &VarTypes,
                             std::map<std::string, z3::expr *> &VarMap) {
  // replace ** with # (pow operator is supported by GAMBA)
  replace_all(expr, "**", "#");

  veque::veque<Token> tokens;
  exprToTokens(expr, tokens, true, &Variables);

  veque::veque<Token> queue;
  shuntingYard(tokens, queue);

  // Create Variables
  std::map<z3::expr *, llvm::Type *> TyMap;
  for (auto &Var : Variables) {
    if (VarMap.find(Var) != VarMap.end()) {
      // Get Type
      auto VarTy = VarTypes[Var];
      TyMap[VarMap[Var]] = VarTy;
      continue;
    }

    auto expr = Z3Ctx.bv_const(Var.c_str(), BitWidth);
    VarMap[Var] = new z3::expr(expr);
  }

  SmallVector<z3::expr, 32> stackAP;

  while (!queue.empty()) {

    const auto token = queue.front();
    queue.pop_front();

    switch (token.type) {
    case Token::Type::Variable: {
      auto c = token.str.c_str();
      auto expr = VarMap[token.str.c_str()];
      stackAP.push_back(*expr);
    } break;
    case Token::Type::Number: {
      // APInt APV(BitWidth, token.str, 10);
      auto ConstExpr = Z3Ctx.bv_val(token.str.c_str(), BitWidth);
      stackAP.push_back(ConstExpr);
    } break;
    case Token::Type::Operator: {
      if (token.unary) {
        // unray operators
        const auto rhsAP = stackAP.back();
        stackAP.pop_back();

        switch (token.str[0]) {
        default: {
          printf("Operator error [%s]\n", token.str.c_str());
          exit(0);
        } break;
        case 'm': // Special operator name for unary '-'
        {
          auto NegExpr = -rhsAP;
          stackAP.push_back(NegExpr);
        } break;
        case '~': {
          auto CompExpr = ~rhsAP;
          stackAP.push_back(CompExpr);
        } break;
        case '!':
          // stackAP.push_back(rhsAP);
          printf("[getZ3ExprFromString] '!' operator not implemented\n");
          exit(-1);
          break;
        }
      } else {
        // binary operators
        auto rhsAP = stackAP.back();
        stackAP.pop_back();

        auto lhsAP = stackAP.back();
        stackAP.pop_back();

        // Align bitwidth
        int lhsBitWidth = lhsAP.get_sort().bv_size();
        int rhsBitWidth = rhsAP.get_sort().bv_size();
        if (lhsBitWidth != rhsBitWidth) {
          if (lhsBitWidth > rhsBitWidth) {
            auto Sort = rhsAP.get_sort();
            rhsAP = z3::zext(rhsAP, lhsBitWidth - rhsBitWidth);
          } else if (lhsBitWidth < rhsBitWidth) {
            auto Sort = lhsAP.get_sort();
            lhsAP = z3::zext(lhsAP, rhsBitWidth - lhsBitWidth);
          }
        }

        switch (token.str[0]) {
        default: {
          printf("Operator error [%s]\n", token.str.c_str());
          exit(0);
        } break;
        case '^': {
          auto XorExpr = lhsAP ^ rhsAP;
          stackAP.push_back(XorExpr);
        } break;
        case '*': {
          auto MulExpr = lhsAP * rhsAP;
          stackAP.push_back(MulExpr);
        } break;
        case '>': {
          auto ShrExpr = z3::lshr(lhsAP, rhsAP);
          stackAP.push_back(ShrExpr);
        } break;
        case '#': {
          // power operator
          z3::sort Sort(Z3Ctx);
          auto PowExpr =
              z3::pw(z3::ubv_to_fpa(lhsAP, Sort), z3::ubv_to_fpa(rhsAP, Sort));
          stackAP.push_back(z3::fpa_to_ubv(PowExpr, BitWidth));
        } break;
        case '/': {
          auto DivExpr = lhsAP / rhsAP;
          stackAP.push_back(DivExpr);
        } break;
        case '&': {
          auto AndExpr = lhsAP & rhsAP;
          stackAP.push_back(AndExpr);
        } break;
        case '|': {
          auto OrExpr = lhsAP | rhsAP;
          stackAP.push_back(OrExpr);
        } break;
        case '+': {
          auto AddExpr = lhsAP + rhsAP;
          stackAP.push_back(AddExpr);
        } break;
        case '-': {
          auto SubExpr = lhsAP - rhsAP;
          stackAP.push_back(SubExpr);
        } break;
        }
      }
    } break;

    default:
      printf("Token error\n");
      exit(0);
    }
  }

  // Aling bitwidth
  auto &ModV = stackAP.back();
  int ModBitWidth = ModV.get_sort().bv_size();
  if (ModBitWidth > BitWidth) {
    auto Sort = ModV.get_sort();
    ModV = ModV.extract(BitWidth - 1, 0);
  } else if (ModBitWidth < BitWidth) {
    auto Sort = ModV.get_sort();
    ModV = z3::zext(ModV, BitWidth - ModBitWidth);
  }

  return ModV;
}
