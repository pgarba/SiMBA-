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

// Timeout in seconds. Bounds the Z3 solver (passed to Z3 in milliseconds)
// and, since Phase 9, also bounds the selected MBA simplifier (see
// SimplifierRouter.cpp: the GeneralSimplifier deadline + Python subprocess).
llvm::cl::opt<int> timeout(
    "timeout", llvm::cl::Optional,
    llvm::cl::desc("Timeout in seconds for the Z3 solver / simplifiers "
                  "(Default 30)"),
    llvm::cl::value_desc("timeout"), llvm::cl::init(30),
    llvm::cl::cat(SiMBAOpt));

// Accept unknown as unsat
llvm::cl::opt<bool> AcceptUnknown(
    "accept-unknown", llvm::cl::Optional,
    llvm::cl::desc("Accept unknown as unsat (Needed on timeout)"),
    llvm::cl::value_desc("accept-unknown"), llvm::cl::init(false),
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

// Global Z3 context for proveReplacement(). Creating a fresh z3::context per
// call is expensive (it allocates the Z3 kernel, sorts, etc.); with the QF_BV
// logic the solve itself is sub-millisecond, so context creation dominates.
// Cache one context for the whole process, like LLVMParser's Z3CtxGlobal.
//
// prove() already handles context switching: if the cached Solver belongs to a
// different context than the conjecture, it resets the solver. So a global
// context here is safe even when prove() is also called from LLVMParser with
// its own Z3CtxGlobal.
static z3::context *ProverCtx = nullptr;

static z3::context &getProverCtx() {
  if (!ProverCtx)
    ProverCtx = new z3::context;
  return *ProverCtx;
}

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
    // Z3's "timeout" parameter is in milliseconds.
    Z3_global_param_set("timeout", std::to_string(timeout * 1000).c_str());

    // Use the QF_BV (quantifier-free bit-vector) logic instead of the old
    // `simplify & bit-blast & smt` pipeline. The bit-blast tactic converts
    // bit-vector ops to boolean circuits and solves with a SAT solver, which
    // is exponential in the bit width for multiplication-heavy expressions
    // (timeouts at 32/64-bit). QF_BV uses word-level reasoning and solves the
    // same expressions in sub-millisecond time (see plans/Z3_SPEEDUP_PLAN.md
    // for the measured 10,000-60,000x speedup).
    //
    // model=false: we only need sat/unsat, not a model. proof=false is the
    // default but is set explicitly for clarity.
    Solver = new z3::solver(c, "QF_BV");
    Solver->set("model", false);
    Solver->set("proof", false);
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
  // Use the cached global context (see getProverCtx) instead of creating a
  // fresh one per call - context creation is expensive and now dominates the
  // sub-millisecond QF_BV solve.
  z3::context &Z3Ctx = getProverCtx();

  // Get Expressions
  std::map<std::string, z3::expr *> VarMap;
  std::map<std::string, llvm::Type *> VarTypes;

  auto Z3Exp0 =
      getZ3ExprFromString(Z3Ctx, expr0, BitWidth, Variables, VarTypes, VarMap);
  auto Z3Exp1 =
      getZ3ExprFromString(Z3Ctx, expr1, BitWidth, Variables, VarTypes, VarMap);

  // Prove
  auto Result = prove(((Z3Exp0 != Z3Exp1)));

  // Clean up variables (the z3::expr handles belong to the global context,
  // which outlives them; dropping the local references is enough).
  for (auto v : VarMap) {
    delete v.second;
  }

  // The cached solver now belongs to the global context, which stays alive -
  // no need to reset it (unlike the old per-call local context).

  return Result;
}