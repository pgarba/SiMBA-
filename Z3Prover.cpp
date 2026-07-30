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

// Global solver to speed up things.
//
// This is only ever valid for the one z3::context it was built from: a
// Z3_solver handle is scoped to its context, and ~solver calls back into
// that context to drop its reference. So this pointer must never outlive
// that context, and must never be handed a conjecture from a different
// one. Both are easy to get wrong from the outside - LLVMParser owns the
// context this normally runs against (Z3CtxGlobal) and can recreate it -
// hence resetZ3Solver/abandonZ3Solver for the owner to call, plus the
// context check in prove() below as a backstop for any caller that brings
// its own context (proveReplacement does).
//
// Getting it wrong is not a graceful failure: calling Z3_solver_reset /
// Z3_solver_assert through a stale handle writes into a freed context and
// corrupts Z3's heap, which then surfaces much later and somewhere else
// entirely - typically an access violation inside Z3_inc_ref, or inside
// Z3_del_context on the next context teardown.
z3::solver *Solver = nullptr;

void resetZ3Solver() {
  delete Solver;
  Solver = nullptr;
}

void abandonZ3Solver() { Solver = nullptr; }

bool prove(z3::expr conjecture) {
  z3::context &c = conjecture.ctx();

  // A solver cached from another (still live) context cannot be reused for
  // this conjecture - drop it and build one for the right context.
  if (Solver && &Solver->ctx() != &c) {
    resetZ3Solver();
  }

  // Create new solver if needed
  if (!Solver) {
    Z3_global_param_set("timeout", timeout.c_str());

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

  // prove() has now cached a solver built from the local Z3Ctx above,
  // which is about to be destroyed - drop it while its context is still
  // alive rather than leaving a stale handle behind for the next caller.
  resetZ3Solver();

  return Result;
}