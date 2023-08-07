#include "Z3Prover.h"

#include <iostream>
#include <map>

#include "ShuttingYard.h"

#include "llvm/IR/Type.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"

llvm::cl::opt<bool>
    PrintSMT("print-smt", llvm::cl::Optional,
             llvm::cl::desc("Print SMT2 formula for debugging purposes"),
             llvm::cl::value_desc("print-smt"), llvm::cl::init(false));

bool prove(z3::expr conjecture) {
  z3::context &c = conjecture.ctx();

  // z3::solver s(c);

  z3::tactic t(c, "qflia");
  auto s = t.mk_solver();

  s.add(!conjecture);

  if (PrintSMT) {
    llvm::outs() << "[SMT2 Start]\n" << s.to_smt2() << "[SMT2 End]\n";
  }

  if (s.check() == z3::unsat) {
    return true;
  } else {
    return false;
  }
}

bool proveReplacement(std::string &expr0, std::string &expr1, int BitWidth,
                      std::vector<std::string> &Variables) {
  z3::context Z3Ctx;

  // Get Expressions
  std::map<std::string, z3::expr *> VarMap;
  std::map<std::string, llvm::Type *> VarTypes;

  auto Z3Exp0 =
      getZ3ExprFromString(Z3Ctx, expr0, BitWidth, Variables, VarTypes, VarMap);
  auto Z3Exp1 =
      getZ3ExprFromString(Z3Ctx, expr1, BitWidth, Variables, VarTypes, VarMap);

  // Prove
  auto Result = prove(((Z3Exp0 == Z3Exp1)));

  // Clean up variables
  for (auto v : VarMap) {
    delete v.second;
  }

  return Result;
}