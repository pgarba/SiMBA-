#include "Z3Prover.h"

#include <iostream>
#include <map>

#include "ShuttingYard.h"

#include "llvm/IR/Type.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"

extern llvm::cl::OptionCategory SiMBAOpt;

llvm::cl::opt<bool> PrintSMT(
    "print-smt", llvm::cl::Optional,
    llvm::cl::desc("Print SMT2 formula for debugging purposes"),
    llvm::cl::value_desc("print-smt"), llvm::cl::init(false),
    llvm::cl::cat(SiMBAOpt));

// Add timeout parameter as string
llvm::cl::opt<std::string> timeout(
    "timeout", llvm::cl::Optional,
    llvm::cl::desc("Timeout for Z3 solver (Default 700)"),
    llvm::cl::value_desc("timeout"), llvm::cl::init("700"),
    llvm::cl::cat(SiMBAOpt));

// Accept unknown as unsat
llvm::cl::opt<bool> AcceptUnknown(
    "accept-unknown", llvm::cl::Optional,
    llvm::cl::desc("Accept unknown as unsat (Needed on timeout)"),
    llvm::cl::value_desc("accept-unknown"), llvm::cl::init(true),
    llvm::cl::cat(SiMBAOpt));

// Global solver to speed up things
z3::solver *Solver = nullptr;

bool prove(z3::expr conjecture) {
  // Create new solver if needed
  if (!Solver) {
    Z3_global_param_set("timeout", timeout.c_str());

    z3::context &c = conjecture.ctx();

    auto t = (z3::tactic(c, "simplify") & z3::tactic(c, "bit-blast") &
              z3::tactic(c, "smt"));
    Solver = new z3::solver(t.mk_solver());
  }

  // reset and add
  Solver->reset();
  Solver->add(conjecture);

  if (PrintSMT) {
    llvm::outs() << "[SMT2 Start]\n" << Solver->to_smt2() << "[SMT2 End]\n";
  }

  auto R = Solver->check();
  if (R == z3::unsat) {
    return true;
  } else if (R == z3::unknown) {
    // Accept unknown as true
    if (AcceptUnknown) {
      return true;
    }
    return false;
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
  auto Result = prove(((Z3Exp0 != Z3Exp1)));

  // Clean up variables
  for (auto v : VarMap) {
    delete v.second;
  }

  return Result;
}