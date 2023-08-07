#include "LLVMParser.h"

#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Threading.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Evaluator.h"

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <z3++.h>

#include "CSiMBA.h"
#include "Modulo.h"
#include "ShuttingYard.h"
#include "Simplifier.h"
#include "Z3Prover.h"
#include "veque.h"

// #define DEBUG_SIMPLIFICATION

using namespace llvm;
using namespace std;
using namespace std::chrono;

llvm::cl::opt<std::string>
    UseExternalSimplifier("external-simplifier", cl::Optional,
                          cl::desc("Path to external simplifier script for "
                                   "simplification (Supports: SiMBA/GAMBA"),
                          cl::value_desc("external-simplifier"), cl::init(""));

namespace LSiMBA {

llvm::LLVMContext LLVMParser::Context;

LLVMParser::LLVMParser(const std::string &filename,
                       const std::string &OutputFile, bool Parallel,
                       bool Verify, bool OptimizeBefore, bool OptimizeAfter,
                       bool Debug, bool Prove)
    : OutputFile(OutputFile), Parallel(Parallel), Verify(Verify),
      OptimizeBefore(OptimizeBefore), OptimizeAfter(OptimizeAfter),
      Debug(Debug), Prove(Prove), SP64(filename.length()), TLII(nullptr),
      TLI(nullptr), M(nullptr), F(nullptr) {
  if (!this->parse(filename)) {
    llvm::errs() << "[!] Error: Could not parse file " << filename << "\n";
    return;
  }

  // Create evaluator
  this->TLII = new TargetLibraryInfoImpl(Triple(M->getTargetTriple()));
  this->TLI = std::make_unique<TargetLibraryInfo>(*TLII);
  this->Eval = std::make_unique<Evaluator>(M->getDataLayout(), TLI.get());

  this->MaxThreadCount = thread::hardware_concurrency();
}

LLVMParser::LLVMParser(llvm::Module *M, bool Parallel, bool Verify,
                       bool OptimizeBefore, bool OptimizeAfter, bool Debug,
                       bool Prove)
    : M(M), F(nullptr), Parallel(Parallel), Verify(Verify),
      OptimizeBefore(OptimizeBefore), OptimizeAfter(OptimizeAfter),
      Debug(Debug), Prove(Prove), SP64((uint64_t)M), TLII(nullptr),
      TLI(nullptr) {
  // Create evaluator

  this->TLII = new TargetLibraryInfoImpl(Triple(M->getTargetTriple()));
  this->TLI = std::make_unique<TargetLibraryInfo>(*TLII);
  this->Eval = std::make_unique<Evaluator>(M->getDataLayout(), TLI.get());

  this->MaxThreadCount = thread::hardware_concurrency();
}

LLVMParser::LLVMParser(llvm::Function *F, bool Parallel, bool Verify,
                       bool OptimizeBefore, bool OptimizeAfter, bool Debug,
                       bool Prove)
    : M(F->getParent()), F(F), Parallel(Parallel), Verify(Verify),
      OptimizeBefore(OptimizeBefore), OptimizeAfter(OptimizeAfter),
      Debug(Debug), Prove(Prove), SP64((uint64_t)M), TLII(nullptr),
      TLI(nullptr) {
  // Create evaluator

  this->TLII = new TargetLibraryInfoImpl(Triple(M->getTargetTriple()));
  this->TLI = std::make_unique<TargetLibraryInfo>(*TLII);
  this->Eval = std::make_unique<Evaluator>(M->getDataLayout(), TLI.get());

  this->MaxThreadCount = thread::hardware_concurrency();
}

LLVMParser::~LLVMParser() {}

int LLVMParser::simplify() {
  int Count = this->extractAndSimplify();

  this->InstructionCountAfter = getInstructionCount(M);

  writeModule();

  return Count;
}

int LLVMParser::simplifyMBAFunctionsOnly() {
  int Count = this->simplifyMBAModule();

  // Disable as it leads to strange crash
  if (this->OptimizeAfter) {
    runLLVMOptimizer();
  }

  writeModule();

  return Count;
}

void LLVMParser::writeModule() {
  if (this->OutputFile.empty())
    return;

  std::error_code EC;
  llvm::raw_fd_ostream OS(this->OutputFile, EC, llvm::sys::fs::OF_None);
  if (EC) {
    outs() << "[!] Could not open file: '" << this->OutputFile << "'\n";
    return;
  }

  OS << *this->M;
  OS.close();

  outs() << "[+] Wrote LLVM Module to: '" << this->OutputFile << "'\n";
}

llvm::LLVMContext &LLVMParser::getLLVMContext() { return LLVMParser::Context; };

bool LLVMParser::hasLoadStores(llvm::Function &F) {
  for (auto &BB : F) {
    for (auto &I : BB) {
      if (isa<LoadInst>(I) || isa<StoreInst>(I)) {
        return true;
      }
    }
  }
  return false;
}

void LLVMParser::initResultVector(llvm::Function &F,
                                  std::vector<llvm::APInt> &ResultVector,
                                  const llvm::APInt &Modulus, int VNumber,
                                  llvm::Type *IntType) {
  auto RetVal = ConstantInt::get(IntType, 0);

  llvm::SmallVector<Constant *, 32> par;
  for (int i = 0; i < pow(2, VNumber); i++) {
    int n = i;
    for (int j = 0; j < VNumber; j++) {
      auto C = ConstantInt::get(IntType, n & 1);
      par.push_back(C);
      n = n >> 1;
    }

    // Evaluate function
    auto R = Eval->EvaluateFunction(&F, RetVal, par);

    // Get Result and store in result vector
    auto CIRetVal = dyn_cast<ConstantInt>(RetVal);
    APInt v = dyn_cast<ConstantInt>(CIRetVal)->getValue();
    auto OldBitWidth = v.getBitWidth();
    if (v.isSignBitSet()) {
      // v = v.srem(Modulus);
      v = v.sextOrTrunc(Modulus.getBitWidth()).srem(Modulus).trunc(OldBitWidth);
    } else {
      // v = v.urem(Modulus);
      v = v.sextOrTrunc(Modulus.getBitWidth()).urem(Modulus).trunc(OldBitWidth);
    }

    // Store value mod modulus
    ResultVector.push_back(v);

    par.clear();
  }

  return;
}

llvm::Instruction *LLVMParser::getSingleTerminator(llvm::Function &F) {
  // Collect all terminators
  std::vector<llvm::Instruction *> Terminators;
  for (auto &BB : F) {
    Terminators.push_back(dyn_cast<Instruction>(BB.getTerminator()));
  }

  // Only allow 1 Terminator
  if (Terminators.size() != 1) {
    std::string ErrMsg = "[!] Error: More than 1 Terminator in function " +
                         F.getName().str() + "\n";
    llvm::report_fatal_error(ErrMsg.c_str());
  }

  return Terminators.front();
}

bool LLVMParser::parse(const std::string &filename) {
  SMDiagnostic Err;

  M = llvm::parseIRFile(filename, Err, Context).release();
  if (!M) {
    llvm::report_fatal_error("[!] Could not read llvm ir file!", false);
  }

  this->InstructionCountBefore = getInstructionCount(M);

  if (this->OptimizeBefore) {
    runLLVMOptimizer(true);
  }

  return true;
}

int LLVMParser::extractAndSimplify() {
  int MBASimplified = 0;

  // Collect all functions
  std::vector<llvm::Function *> Functions;
  for (auto &F : *M) {
    // If F set only work on F
    if (this->F && (&F != this->F)) {
      continue;
    }

    // Skip simplifed functions
    if (F.getName().startswith("MBA_Simp")) {
      continue;
    }

    Functions.push_back(&F);
  }

  // Walk through all functions
  if (this->Debug) {
    outs() << "[+] Simplifying " << Functions.size() << " function(s) ...\n";
  }

  auto start = high_resolution_clock::now();
  for (auto F : Functions) {
    if (F->isDeclaration())
      continue;

    if (F->getName().contains("_keep")) {
      if (this->Debug) {
        outs() << "[!] Skipping simplification of function: " << F->getName()
               << "\n";
      }
      continue;
    }

    if (this->Debug) {
      outs() << "[*] Simplifying function: " << F->getName() << "\n";
    }

    int MBACount = 0;
    bool Found = false;

    DominatorTree DT(*F);

    // Clone function to compare
    ValueToValueMapTy VMap;
    auto FClone = CloneFunction(F, VMap);

    // Measure Time
    auto start = high_resolution_clock::now();

    // Get candidates
    std::vector<MBACandidate> Candidates;
    this->extractCandidates(*F, Candidates);

    // Find valid replacements for candidates
    Found = this->findReplacements(&DT, Candidates);

    // Apply replacements and optimize
    for (int i = 0; i < Candidates.size(); i++) {
      if (Candidates[i].isValid == false)
        continue;

      if (this->Debug) {
        printAST(Candidates[i].AST);
        if (this->Debug) {
          outs() << "[!] Simplification: '" << Candidates[i].Replacement
                 << " with " << countOperators(Candidates[i].Replacement)
                 << " operators!\n";
        }
      }

      std::vector<std::string> VNames;
      char ArgName = 'a';
      for (int j = 0; j < Candidates[i].Variables.size(); j++) {
        VNames.push_back(std::string(1, ArgName++));
      }

      createLLVMReplacement(
          Candidates[i].Candidate, Candidates[i].Candidate->getType(),
          Candidates[i].Replacement, VNames, Candidates[i].Variables);

      MBASimplified++;
      MBACount++;
    }

    // When we reach this here, replacements are valid
    FClone->eraseFromParent();
  }

  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(stop - start);
  if (this->Debug) {
    outs() << "[+] Done! " << MBASimplified << " MBAs simplified ("
           << duration.count() << " ms)\n";
  }

  // Optimize if any replacements
  if (MBASimplified && this->OptimizeBefore) {
    this->runLLVMOptimizer();
  }

  return MBASimplified;
}

int LLVMParser::simplifyMBAModule() {
  // Collect all functions
  std::vector<llvm::Function *> Functions;
  for (auto &F : *M) {
    if (this->F && (&F != this->F))
      continue;

    // Skip simplifed functions
    if (F.getName().startswith("MBA_Simp"))
      continue;

    // Check if any load/stores are in the function
    if (hasLoadStores(F))
      report_fatal_error("[!] Error: Function contains load/stores!");

    Functions.push_back(&F);
  }

  // Walk through all functions
  outs() << "[+] Simplifying " << Functions.size() << " functions ...\t";

  auto start = high_resolution_clock::now();
  for (auto F : Functions) {
    // Get the terminator
    auto Terminator = getSingleTerminator(*F);

    int BitWidth = Terminator->getOperand(0)->getType()->getIntegerBitWidth();

    auto Modulus = getModulus(BitWidth);

    // Collect the arguments
    std::vector<std::string> VNames;
    SmallVector<llvm::Value *, 8> Variables;
    char ArgName = 'a';
    for (auto &Arg : F->args()) {
      Variables.push_back(&Arg);
      VNames.push_back(std::string(1, ArgName++));
    }

    auto RetTy = Terminator->getOperand(0)->getType();
    auto VNumber = Variables.size();

    // Calc the result vector
    std::vector<APInt> ResultVector;
    this->initResultVector(*F, ResultVector, Modulus, VNumber, RetTy);

    // Simpify MBA
    Simplifier S(BitWidth, false, VNumber, ResultVector);

    std::string SimpExpr;
    S.simplify(SimpExpr, false, false);

    // Convert simplified expression to LLVM IR
    auto FSimp = createLLVMFunction(this->M, RetTy, SimpExpr, VNames);

    // Verify if simplification is valid
    if (this->Verify && !this->verify(F, FSimp, Modulus)) {
      outs() << "[!] Error: Simplification is not valid for function "
             << F->getName() << "\n";
    }

    // Debug out
    if (this->Debug) {
      outs() << "\n[*] Simplified Expression: " << SimpExpr << "\n";
    }
  }

  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(stop - start);

  outs() << "Done! (" << duration.count() << " ms)\n";

  return Functions.size();
}

bool LLVMParser::verify(llvm::Function *F0, llvm::Function *F1,
                        llvm::APInt &Modulus) {
  // Check functions have the same amount of arguments
  if (F0->arg_size() != F1->arg_size()) {
    return false;
  }

  // Check if types are the sames
  if (F0->getReturnType() != F1->getReturnType()) {
    return false;
  }

  // Check if argument types are the same
  if (F0->arg_size() != F1->arg_size()) {
    return false;
  }

  for (int i = 0; i < F0->arg_size(); i++) {
    if (F0->getArg(i)->getType() != F1->getArg(i)->getType()) {
      return false;
    }
  }

  auto RetTy = F0->getReturnType();
  auto RetVal0 = ConstantInt::get(RetTy, 0);
  auto RetVal1 = ConstantInt::get(RetTy, 0);

  auto vnumber = F0->arg_size();
  llvm::SmallVector<Constant *, 16> par;
  for (int i = 0; i < NUM_TEST_CASES; i++) {
    for (int j = 0; j < vnumber; j++) {
      auto C = ConstantInt::get(RetTy, SP64.next());
      par.push_back(C);
    }

    Eval->EvaluateFunction(F0, RetVal0, par);
    Eval->EvaluateFunction(F1, RetVal1, par);

    auto R0 = dyn_cast<ConstantInt>(RetVal0)
                  ->getValue()
                  .zextOrTrunc(Modulus.getBitWidth())
                  .urem(Modulus)
                  .getLimitedValue();
    auto R1 = dyn_cast<ConstantInt>(RetVal1)
                  ->getValue()
                  .zextOrTrunc(Modulus.getBitWidth())
                  .urem(Modulus)
                  .getLimitedValue();

    if (R0 != R1) {
      return false;
    }

    par.clear();
  }

  return true;
}

bool LLVMParser::verify(llvm::SmallVectorImpl<BFSEntry> &AST,
                        std::string &SimpExpr,
                        llvm::SmallVectorImpl<llvm::Value *> &Variables) {
  int VNumber = Variables.size();
  int BitWidth = AST.front().I->getType()->getIntegerBitWidth();
  auto Modulus = getModulus(BitWidth);

  std::string Expr1_replVar = SimpExpr;
  for (int i = 0; i < Variables.size(); i++) {
    char Var = 'a' + i;
    string StrVar(1, Var);

    Simplifier::replaceAllStrings(Expr1_replVar, StrVar,
                                  "X[" + std::to_string(i) + "]");
  }

  // The number of operations in the new expressions
  int Operations = 0;

  llvm::SmallVector<APInt, 16> par;
  llvm::SmallVector<int64_t, 16> parInt;
  for (int i = 0; i < NUM_TEST_CASES; i++) {
    for (int j = 0; j < VNumber; j++) {
      auto v = SP64.next();
      par.push_back(APInt(BitWidth, v));
      parInt.push_back(v);
    }

    // Eval AST
    bool Error = false;
    auto AP_R0 = this->evaluateAST(AST, Variables, par, Error);
    if (Error) {
      return false;
    }

    // Mod
    /*
    if (AP_R0.isSignBitSet()) {
      AP_R0 = AP_R0.srem(Modulus);
    } else {
      AP_R0 = AP_R0.urem(Modulus);
    }
    */

    // Eval replacement
    auto AP_R1 = eval(Expr1_replVar, par, BitWidth, &Operations);
    /*
    if (AP_R1.isSignBitSet()) {
      AP_R1 = AP_R1.srem(Modulus);
    } else {
      AP_R1 = AP_R1.urem(Modulus);
    }
    */

    if (AP_R0.getSExtValue() != AP_R1.getSExtValue()) {
#ifdef DEBUG_SIMPLIFICATION
      outs() << "[!] Error: Verification failed for: " << SimpExpr << "\n";
#endif
      return false;
    }

    par.clear();
  }

  // Check if replacement is cheaper than original expression
  if (AST.size() <= Operations) {
#ifdef DEBUG_SIMPLIFICATION
    outs() << "[!] Simpl is no improvement! (" << AST.size()
           << " <= " << Operations << ")\n";
#endif
    return false;
  }

  // Prove with z3
  if (this->Prove) {
    z3::context Z3Ctx;

    // Build Variable replacements
    std::vector<std::string> Vars;
    std::map<std::string, llvm::Type *> VarTypes;
    for (int i = 0; i < Variables.size(); i++) {
      char c = 'a' + i;
      string strC = string(1, c);
      Vars.push_back(strC);

      VarTypes[strC] = Variables[i]->getType();
    }

    if (this->Debug) {
      outs() << "[Z3] Proving ...\n";
    }

    // Get Z3 expressions
    std::map<std::string, z3::expr *> VarMap;
    auto Z3Exp0 =
        getZ3ExpressionFromAST(Z3Ctx, AST, Variables, VarMap, BitWidth);

    // Ensure BitWidth matches
    if (Z3Exp0.get_sort().bv_size() != BitWidth) {
      report_fatal_error("[!] Error: Z3Exp0 BitWidth does not match!");
    }

    auto Z3Exp1 =
        getZ3ExprFromString(Z3Ctx, SimpExpr, BitWidth, Vars, VarTypes, VarMap);

    if (Z3Exp1.get_sort().bv_size() != BitWidth) {
      report_fatal_error("[!] Error: Z3Exp1 BitWidth does not match!");
    }

    // Prove expressions
    auto start = high_resolution_clock::now();
    auto Result = prove(((Z3Exp0 == Z3Exp1)));
    auto stop = high_resolution_clock::now();

    if (this->Debug) {
      auto duration = duration_cast<milliseconds>(stop - start);

      outs() << "[Z3] Proved in " << duration.count()
             << " ms Result (1 == valid): " << Result << "\n";
    }

    return Result;
  }

  // Otherwise don't apply this replacement
  return true;
}

bool LLVMParser::runLLVMOptimizer(bool Initial) {
  llvm::legacy::PassManager module_manager;

  if (Initial) {
    outs() << "[+] Running LLVM optimizer (Some MBAs might already be "
              "simplified by that!) ...\t\t";
  } else {
    outs() << "[+] Running LLVM optimizer ...\t\t";
  }

  llvm::PassManagerBuilder builder;
  builder.OptLevel = 3;
  builder.SizeLevel = 2;
  builder.LibraryInfo = TLII;
  builder.DisableUnrollLoops = true;
  builder.MergeFunctions = false;
  builder.VerifyInput = false;
  builder.VerifyOutput = false;

  builder.populateModulePassManager(module_manager);

  auto start = high_resolution_clock::now();

  module_manager.run(*this->M);

  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(stop - start);

  rewriteIntrinsics();

  outs() << " Done! (" << duration.count() << " ms)\n";

  return true;
}

bool LLVMParser::rewriteIntrinsics() {
  bool Changes = false;

  std::map<std::string, std::function<bool()>> Transformations;

  // Todo: %39 = tail call i64 @llvm.bswap.i64(i64 %38)

  // Transform llvm.fshl.i64 to rotate left code (if possible)
  Transformations.emplace("llvm.fshl.i64", [&]() -> bool {
    auto Intri = this->M->getFunction("llvm.fshl.i64");
    if (!Intri)
      return false;

    SmallVector<llvm::Instruction *, 4> ToDelete;

    for (auto U : Intri->users()) {
      auto CI = dyn_cast<CallInst>(U);
      if (!CI)
        continue;

      // Arg0 and Arg1 has to be equal to be a rotl
      if (CI->getArgOperand(0) != CI->getArgOperand(0))
        continue;

      /*
        %3 = shl i64 %0, %1
        %4 = sub i64 64, %1
        %5 = lshr i64 %0, %4
        %6 = or i64 %5, %3
      */
      // Create the builder to build
      llvm::IRBuilder<> Builder(CI);

      auto Op = CI->getArgOperand(0);
      auto Count = CI->getArgOperand(2);
      auto t3 = Builder.CreateShl(Op, Count);
      auto t4 = Builder.CreateSub(
          llvm::ConstantInt::get(Count->getType(),
                                 Count->getType()->getIntegerBitWidth()),
          Count);
      auto t5 = Builder.CreateLShr(Op, t4);
      auto t6 = Builder.CreateOr(t5, t3);

      CI->replaceAllUsesWith(t6);

      ToDelete.push_back(CI);
    }

    // Cleanup
    for (auto CI : ToDelete) {
      CI->eraseFromParent();
    }

    return true;
  });

  // Apply transformation
  for (auto T : Transformations) {
    Changes |= T.second();
  }

  return Changes;
}

bool LLVMParser::isSupportedInstruction(llvm::Value *V) {
  // We dont support AShr as GAMBA does not support it, for now
  if (auto BO = dyn_cast<BinaryOperator>(V)) {
    if (BO->getOpcode() == Instruction::AShr) {
      return false;
    }

    return ConstantExpr::isSupportedBinOp(BO->getOpcode());
  }

  if (isa<TruncInst>(V)) {
    return true;
  }

  if (isa<ZExtInst>(V)) {
    return true;
  }

  if (isa<SExtInst>(V)) {
    return true;
  }

  return false;
}

void LLVMParser::extractCandidates(llvm::Function &F,
                                   std::vector<MBACandidate> &Candidates) {
  // Instruction to look for 'store', 'select', 'gep', 'icmp', 'ret'
  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    switch (I->getOpcode()) {
    case Instruction::Store: {
      // Check Candidate
      auto SI = dyn_cast<StoreInst>(&*I);
      auto Op = SI->getValueOperand();
      if (isSupportedInstruction(Op)) {
        MBACandidate Cand;
        Cand.Candidate = dyn_cast<Instruction>(Op);
        Candidates.push_back(Cand);
      }
    } break;
    case Instruction::Call: {
      auto CI = dyn_cast<CallInst>(&*I);
      for (unsigned int i = 0; i < CI->arg_size(); i++) {
        if (isSupportedInstruction(CI->getArgOperand(i)->stripPointerCasts())) {
          MBACandidate Cand;
          Cand.Candidate =
              dyn_cast<Instruction>(CI->getArgOperand(i)->stripPointerCasts());
          Candidates.push_back(Cand);
        }
      }
    } break;
    case Instruction::ICmp:
    case Instruction::Select: {
      for (unsigned int i = 0; i < I->getNumOperands(); i++) {
        if (isSupportedInstruction(I->getOperand(i)->stripPointerCasts())) {
          MBACandidate Cand;
          Cand.Candidate =
              dyn_cast<Instruction>(I->getOperand(i)->stripPointerCasts());
          Candidates.push_back(Cand);
        }
      }
    } break;
    case Instruction::GetElementPtr: {
      auto GEP = dyn_cast<GetElementPtrInst>(&*I);
      auto Index = GEP->getOperand(GEP->getNumOperands() - 1);

      if (isSupportedInstruction(Index->stripPointerCasts())) {
        MBACandidate Cand;
        Cand.Candidate = dyn_cast<Instruction>(Index->stripPointerCasts());
        Candidates.push_back(Cand);
      }
    } break;
    case Instruction::Ret: {
      auto RI = dyn_cast<ReturnInst>(&*I);
      if (!RI->getReturnValue())
        continue;

      if (isSupportedInstruction(RI->getReturnValue()->stripPointerCasts())) {
        MBACandidate Cand;
        Cand.Candidate =
            dyn_cast<Instruction>(RI->getReturnValue()->stripPointerCasts());
        Candidates.push_back(Cand);
      }
    } break;
    case Instruction::PHI: {
      auto Phi = dyn_cast<PHINode>(&*I);
      for (auto &Inc : Phi->incoming_values()) {
        if (isSupportedInstruction(Inc->stripPointerCasts())) {
          MBACandidate Cand;
          Cand.Candidate = dyn_cast<Instruction>(Inc->stripPointerCasts());
          Candidates.push_back(Cand);
        }
      }
    } break;
    /* Disable as no use for now
    case Instruction::Add:
    case Instruction::Sub:
    case Instruction::Xor:
    case Instruction::Or:
    case Instruction::And:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
    case Instruction::Mul: {
      MBACandidate Cand;
      Cand.Candidate = dyn_cast<BinaryOperator>(&*I);
      Candidates.push_back(Cand);
    }
    */
    default: {
      // Skip
    }
    }
  }
}

bool LLVMParser::findReplacements(llvm::DominatorTree *DT,
                                  std::vector<MBACandidate> &Candidates) {
  if (Candidates.empty()) {
    return false;
  }

  bool ReplacementFound = false;

#ifdef DEBUG_SIMPLIFICATION
  // Debug out
  Candidates.front().Candidate->getFunction()->print(outs());
#endif

  // Search for replacements
  std::vector<MBACandidate> SubASTCandidates;
  for (int i = 0; i < Candidates.size(); i++) {
    auto &Cand = Candidates[i];
    getAST(DT, Cand.Candidate, Cand.AST, Cand.Variables, true);

    int s = Cand.AST.size();
    if (Cand.AST.size() < 2) {
      continue;
    }

#ifdef DEBUG_SIMPLIFICATION
    // Debug out
    printAST(Cand.AST);

    // Debug print variables
    outs() << "[*] Variables:\n";
    for (auto Var : Cand.Variables) {
      Var->print(outs());
      outs() << "\n";
    }
#endif

    // Only handle max 6 Vars
    if (Cand.Variables.size() > 6) {
      Cand.isValid = false;
      continue;
    }

    // Dont work on vector types
    if (Cand.AST.front().I->getType()->isVectorTy()) {
      Cand.isValid = false;
      continue;
    }

    // Try to simplify the whole AST
    int BitWidth = Cand.AST.front().I->getType()->getIntegerBitWidth();
    if (BitWidth == 0 || BitWidth > 64) {
      // If BitWidth is zero then stop here
      return false;
    }
    auto Modulus = getModulus(BitWidth);

    std::vector<APInt> ResultVector;
    initResultVectorFromAST(Cand.AST, ResultVector, Modulus, Cand.Variables);

    // Simpify MBA
    Simplifier S(BitWidth, false, Cand.Variables.size(), ResultVector);
    bool SkipVerify = false;

    if (!UseExternalSimplifier.empty()) {
      std::string &Path = UseExternalSimplifier;
      auto Expr = getASTAsString(Cand.AST, Cand.Variables);

      if (this->Debug) {
        outs() << "[*] Using external simplifier\n";
        outs() << "[*] External simplified expression (BitWidth: " << BitWidth
               << ") from '" << Expr << "'\n";
      }

      auto R = S.external_simplifier(Expr, Cand.Replacement, false, false, Path,
                                     BitWidth, this->Debug);
      if (R) {
        if (this->Debug) {
          outs() << "[*] to '" << Cand.Replacement << "'\n";
        }
      } else {
        // Skip verify and walk sub ast
        Cand.isValid = false;
        SkipVerify = true;

        if (this->Debug) {
          outs() << "[*] Failed!\n";
        }
      }
    } else {
      S.simplify(Cand.Replacement, false, false);
    }

    // Verify is replacement is valid
    if (!SkipVerify) {
      Cand.isValid = this->verify(Cand.AST, Cand.Replacement, Cand.Variables);
    }

    if (Cand.isValid == false) {
      // Could not simplify the whole AST so walk through SubASTs
      ReplacementFound = walkSubAST(DT, Cand.AST, SubASTCandidates);
    } else {
      if (this->Debug) {
        outs() << "[*] Full AST Simplified Expression: " << Cand.Replacement
               << "\n";
      }

      ReplacementFound = true;
    }
  }

  // Clean up Candidates and keep only valid ones
  std::vector<MBACandidate> ValidCandidates;

  for (auto &C : Candidates) {
    if (!C.isValid)
      continue;

    ValidCandidates.push_back(C);
  }

  // Merge candidates with new candidates
  for (auto &C : SubASTCandidates) {
    if (!C.isValid)
      continue;

    ValidCandidates.push_back(C);
  }

  Candidates = ValidCandidates;

  return ReplacementFound;
}

bool LLVMParser::walkSubAST(llvm::DominatorTree *DT,
                            llvm::SmallVectorImpl<BFSEntry> &AST,
                            std::vector<MBACandidate> &Candidates) {
  bool Valid = false;

  // Walk forward
  for (auto &E : AST) {
    // Walk the operands
    for (auto &Op : E.I->operands()) {
      auto BinOp = dyn_cast<BinaryOperator>(Op);
      if (!BinOp)
        continue;

      // Only work on supported operands
      if (ConstantExpr::isSupportedBinOp(BinOp->getOpcode()) == false)
        continue;

      MBACandidate C;
      C.Candidate = BinOp;
      this->getAST(DT, BinOp, C.AST, C.Variables, true);

      if (C.AST.size() < 2)
        continue;

      int BitWidth = C.AST.front().I->getType()->getIntegerBitWidth();

      auto Modulus = getModulus(BitWidth);

      std::vector<APInt> ResultVector;
      initResultVectorFromAST(C.AST, ResultVector, Modulus, C.Variables);

      // Simplify MBA
      bool SkipVerify = false;
      Simplifier S(BitWidth, false, C.Variables.size(), ResultVector);

      if (!UseExternalSimplifier.empty()) {
        std::string &Path = UseExternalSimplifier;
        auto Expr = getASTAsString(C.AST, C.Variables);

        if (this->Debug) {
          outs() << "[*] Using external simplifier\n";
          outs() << "[*] External simplified expression (BitWidth: " << BitWidth
                 << ") from '" << Expr << "'\n";
        }

        auto R = S.external_simplifier(Expr, C.Replacement, false, false, Path,
                                       BitWidth, this->Debug);
        if (R == false) {
          SkipVerify = true;
          C.isValid = false;
        }
      } else {
        S.simplify(C.Replacement, false, false);
      }

#ifdef DEBUG_SIMPLIFICATION
      if (!SkipVerify) {
        printAST(C.AST);
        outs() << "[*] Simplified Expression: " << C.Replacement << "\n";
      }
#endif

      if (!SkipVerify)
        C.isValid = this->verify(C.AST, C.Replacement, C.Variables);

      if (C.isValid) {
        // Store valid replacement
        Candidates.push_back(C);

        Valid = true;

        // Stop here
        return Valid;
      }
    }
  }

  return Valid;
}

void LLVMParser::initResultVectorFromAST(
    llvm::SmallVectorImpl<BFSEntry> &AST,
    std::vector<llvm::APInt> &ResultVector, const llvm::APInt &Modulus,
    llvm::SmallVectorImpl<llvm::Value *> &Variables) {
  // Evalute AST
  int VNumber = Variables.size();
  auto BitWidth = AST.front().I->getType()->getIntegerBitWidth();

  SmallVector<APInt, 16> Par;
  for (int i = 0; i < pow(2, Variables.size()); i++) {
    int n = i;
    for (int j = 0; j < VNumber; j++) {
      Par.push_back(APInt(BitWidth, n & 1));
      n = n >> 1;
    }

    // Evaluate function
    bool Error = false;
    auto v = evaluateAST(AST, Variables, Par, Error);

    /*
    if (v.isSignBitSet()) {
      v = v.srem(Modulus);
    } else {
      v = v.urem(Modulus);
    }
    */

    // Store value mod modulus
    ResultVector.push_back(v);

    // Clear par again
    Par.clear();
  }
}

void LLVMParser::printAST(llvm::SmallVectorImpl<BFSEntry> &AST) {
  outs() << "[*] AST (Operators: " << AST.size() << "):\n";

  for (auto &e : AST) {
    outs() << e.Depth << ": ";
    e.I->print(outs());
    outs() << "\n";
  }
}

std::string
LLVMParser::getASTAsString(llvm::SmallVectorImpl<BFSEntry> &AST,
                           llvm::SmallVectorImpl<llvm::Value *> &Variables) {

  // Variable map
  std::map<llvm::Value *, std::string> VariableMap;
  char VStr = 'a';
  for (auto &V : Variables) {
    VariableMap[V] = VStr;
    VStr++;
  }

  // Sub Expression Stack
  std::stack<std::string> ExprStack;

  for (auto E = AST.rbegin(); E != AST.rend(); ++E) {
    auto &e = *E;

    std::string Expr = "(";

    auto CurInst = dyn_cast<Instruction>(e.I);

    if (auto BinOp = dyn_cast<BinaryOperator>(e.I)) {

      switch (BinOp->getNumOperands()) {
      // Add later
      case 1:
        // Add later
        report_fatal_error("Unsupported number of operands!");
        break;
      case 2:
        if (auto C = dyn_cast<ConstantInt>(BinOp->getOperand(0))) {
          SmallString<16> StrC;
          C->getValue().toString(StrC, 10, true);
          Expr += StrC;
        } else {
          Expr += VariableMap[BinOp->getOperand(0)];
        }
        break;
      default:
        e.I->dump();
        report_fatal_error("Unsupported number of operands!");
      }

      switch (BinOp->getOpcode()) {
      case Instruction::Add:
        Expr += " + ";
        break;
      case Instruction::Sub:
        Expr += " - ";
        break;
      case Instruction::Mul:
        Expr += " * ";
        break;
      case Instruction::UDiv:
        Expr += " / ";
        break;
      case Instruction::SDiv:
        Expr += " / ";
        break;
      case Instruction::URem:
        Expr += " % ";
        break;
      case Instruction::SRem:
        Expr += " % ";
        break;
      case Instruction::Shl:
        Expr += " << ";
        break;
      case Instruction::LShr:
        Expr += " >> ";
        break;
      case Instruction::AShr:
        // Should work in python...
        Expr += " >> ";
        break;
      case Instruction::Xor:
        Expr += " ^ ";
        break;
      case Instruction::And:
        Expr += " & ";
        break;
      case Instruction::Or:
        Expr += " | ";
        break;
      default:
        e.I->dump();
        report_fatal_error("[getASTAsString] Unsupported binary operator!");
      }

      switch (CurInst->getNumOperands()) {
      case 1:
        // Print operand
        if (auto C = dyn_cast<ConstantInt>(CurInst->getOperand(0))) {
          SmallString<16> StrC;
          C->getValue().toString(StrC, 10, true);
          Expr += StrC;
        } else {
          Expr += VariableMap[CurInst->getOperand(0)];
        }
        break;
      case 2:
        if (auto C = dyn_cast<ConstantInt>(CurInst->getOperand(1))) {
          SmallString<16> StrC;
          C->getValue().toString(StrC, 10, true);
          Expr += StrC;
        } else {
          Expr += VariableMap[CurInst->getOperand(1)];
        }
        break;
      default:
        e.I->dump();
        report_fatal_error("Unsupported number of operands!");
      }
    } else if (auto Trunc = dyn_cast<TruncInst>(CurInst)) {
      Expr += " (";
      Expr += VariableMap[Trunc->getOperand(0)];
      Expr += " & " + to_string(getMASK(Trunc->getDestTy())) + ")";
    } else if (auto ZExt = dyn_cast<ZExtInst>(CurInst)) {
      auto Op0 = ZExt->getOperand(0);

      // Expr += " (";
      Expr += VariableMap[Op0];
      // Expr += " & " + to_string(getMASK(ZExt->getSrcTy())) + ")";
    } else if (auto SExt = dyn_cast<SExtInst>(CurInst)) {
      auto Op0 = SExt->getOperand(0);

      Expr += " (";
      Expr += VariableMap[Op0];

      // 8bit: (x & 0x7f) - (x & 0x80)
      // 16bit: (x & 0x7fff) - (x & 0x8000)
      // 32bit: (x & 0x7fffffff) - (x & 0x80000000)
      // 64bit: (x & 0x7fffffffffffffff) - (x & 0x8000000000000000)
      switch (SExt->getSrcTy()->getIntegerBitWidth()) {
      case 8:
        Expr += " & 0x7f) - (";
        Expr += VariableMap[Op0];
        Expr += " & 0x80)";
        break;
      case 16:
        Expr += " & 0x7fff) - (";
        Expr += VariableMap[Op0];
        Expr += " & 0x8000)";
        break;
      case 32:
        Expr += " & 0x7fffffff) - (";
        Expr += VariableMap[Op0];
        Expr += " & 0x80000000)";
        break;
      case 64:
        Expr += " & 0x7fffffffffffffff) - (";
        Expr += VariableMap[Op0];
        Expr += " & 0x8000000000000000)";
        break;
      default:
        report_fatal_error("Unsupported bit width!");
      }
    } else {
      report_fatal_error("Unsupported instruction!");
    }

    Expr += ") ";

    VariableMap[e.I] = Expr;
    ExprStack.push(Expr);
  }

  return ExprStack.top();
}

uint64_t LLVMParser::getMASK(llvm::Type *Ty) {
  uint64_t Mask = 0;

  if (Ty->isIntegerTy()) {
    Mask = ((uint64_t)1 << Ty->getIntegerBitWidth()) - 1;
  } else if (Ty->isVectorTy()) {
    auto *VTy = cast<VectorType>(Ty);
    Mask = (1 << VTy->getElementType()->getIntegerBitWidth()) - 1;
  } else {
    report_fatal_error("Unsupported type!");
  }

  return Mask;
}

void LLVMParser::getAST(llvm::DominatorTree *DT, llvm::Instruction *I,
                        llvm::SmallVectorImpl<BFSEntry> &AST,
                        llvm::SmallVectorImpl<llvm::Value *> &Variables,
                        bool KeepRoot) {
  // Only work on supported operands
  if (isSupportedInstruction(I) == false) {
    return;
  }

  // Walk the AST in BFS
  std::deque<llvm::Value *> Q;
  std::set<llvm::Value *> Dis;
  std::unordered_map<llvm::Value *, int> DepthMap;
  std::unordered_set<llvm::Value *> Vars;

  int Depth = 0;

  // Mark root as discovered
  Dis.insert(I);

  if (KeepRoot) {
    DepthMap[I] = Depth;
    AST.push_back(BFSEntry(Depth, I));
  } else {
    DepthMap[I] = Depth - 1;
  }

  // Run BFS
  Q.push_front(I);
  while (!Q.empty()) {
    auto v = Q.back();
    Q.pop_back();

    // We are only following instructions
    auto Ins = dyn_cast<Instruction>(v);
    if (!Ins)
      continue;

    for (auto &O : Ins->operands()) {
      if (isa<Constant>(O))
        continue;

      // Must be a variable
      if (isa<Argument>(O)) {
        Vars.insert(O);
        continue;
      }

      if (auto OpIns = dyn_cast<Instruction>(O->stripPointerCasts())) {
        if (Dis.find(OpIns) == Dis.end()) {

          // Check if supported
          if (!isSupportedInstruction(OpIns)) {
            // Use as variable
            Vars.insert(OpIns);
            continue;
          }

          Dis.insert(OpIns);
          Q.push_front(OpIns);

          DepthMap[OpIns] = DepthMap[v] + 1;
          AST.push_back(BFSEntry(DepthMap[v] + 1, OpIns));
        }
      } else {
        // Investigate
        O->print(outs());
        report_fatal_error("Unknown Inst!", false);
      }
    }
  }

  // Sort AST
  std::sort(AST.begin(), AST.end(), [&](BFSEntry &a, BFSEntry &b) {
    return doesDominateInst(DT, a.I, b.I);
  });

  std::reverse(AST.begin(), AST.end());

  // Fill Variables
  for (auto V : Vars) {
    Variables.push_back(V);
  }

  // Sort
  std::sort(Variables.begin(), Variables.end());
}

llvm::APInt
LLVMParser::evaluateAST(llvm::SmallVectorImpl<BFSEntry> &AST,
                        llvm::SmallVectorImpl<llvm::Value *> &Variables,
                        llvm::SmallVectorImpl<APInt> &Par, bool &Error) {
  Constant *InstResult = nullptr;
  llvm::DenseMap<llvm::Value *, llvm::Constant *> ValueStack;

  for (auto E = AST.rbegin(); E != AST.rend(); ++E) {
    auto CurInst = E->I;

    if (auto BO = dyn_cast<BinaryOperator>(CurInst)) {
      if (ConstantExpr::isSupportedBinOp(BO->getOpcode()) == false) {
        CurInst->dump();
        report_fatal_error("[!] Not supported binary operator!", false);
        Error = true;
        return APInt(1, 0);
      }

      InstResult = ConstantExpr::get(
          BO->getOpcode(),
          getVal(BO->getOperand(0), ValueStack, Variables, Par),
          getVal(BO->getOperand(1), ValueStack, Variables, Par));
    } else if (auto Trunc = dyn_cast<TruncInst>(CurInst)) {
      // %27 = trunc i64 %26 to i32
      InstResult = ConstantExpr::getTrunc(
          getVal(Trunc->getOperand(0), ValueStack, Variables, Par),
          Trunc->getType());
    } else if (auto ZExt = dyn_cast<ZExtInst>(CurInst)) {
      InstResult = ConstantExpr::getZExt(
          getVal(ZExt->getOperand(0), ValueStack, Variables, Par),
          ZExt->getType());
    } else if (auto SExt = dyn_cast<SExtInst>(CurInst)) {
      InstResult = ConstantExpr::getSExt(
          getVal(SExt->getOperand(0), ValueStack, Variables, Par),
          SExt->getType());
    } else {
      CurInst->dump();
      report_fatal_error("[!] Not supported instruction!", false);
    }

    ValueStack[CurInst] = InstResult;
  }

  auto CI = dyn_cast<ConstantInt>(InstResult);
  if (!CI) {
    // Value might become poison so take care of this
    Error = true;
    return APInt(1, 0);
  }

  Error = false;
  return CI->getValue();
}

llvm::Constant *
LLVMParser::getVal(llvm::Value *V,
                   llvm::DenseMap<llvm::Value *, llvm::Constant *> &ValueStack,
                   llvm::SmallVectorImpl<llvm::Value *> &Variables,
                   llvm::SmallVectorImpl<llvm::APInt> &Par) {
  if (Constant *CV = dyn_cast<Constant>(V))
    return CV;

  // Check if variable
  int i = 0;
  for (auto Var : Variables) {
    if (Var != V) {
      i++;
      continue;
    }

    // Check if Type is different
    if (Par[i].getBitWidth() > V->getType()->getIntegerBitWidth()) {
      return llvm::ConstantInt::get(
          V->getType(), Par[i].trunc(V->getType()->getIntegerBitWidth()));
    } else if (Par[i].getBitWidth() < V->getType()->getIntegerBitWidth()) {
      return llvm::ConstantInt::get(
          V->getType(), Par[i].zext(V->getType()->getIntegerBitWidth()));
    } else {
      return llvm::ConstantInt::get(V->getType(), Par[i]);
    }
  }

  if (ValueStack.count(V) == 0) {
    report_fatal_error("V not found!");
  }

  return ValueStack[V];
}

bool LLVMParser::doesDominateInst(DominatorTree *DT, const Instruction *InstA,
                                  const Instruction *InstB) {
  // Use ordered basic block in case the 2 instructions are in the same
  // block.
  if (InstA->getParent() == InstB->getParent())
    return InstA->comesBefore(InstB);

  DomTreeNode *DA = DT->getNode(InstA->getParent());
  DomTreeNode *DB = DT->getNode(InstB->getParent());
  return DA->getLevel() < DB->getLevel();
}

z3::expr LLVMParser::getZ3ExpressionFromAST(
    z3::context &Z3Ctx, llvm::SmallVectorImpl<BFSEntry> &AST,
    llvm::SmallVectorImpl<llvm::Value *> &Variables,
    std::map<std::string, z3::expr *> &VarMap, int OverrideBitWidth) {
  llvm::DenseMap<llvm::Value *, z3::expr *> ValueMAP;

  // Create Variables
  char Var = 'a';
  for (auto V : Variables) {
    string VarStr = string(1, Var);

    auto VExpr =
        Z3Ctx.bv_const(VarStr.c_str(), V->getType()->getIntegerBitWidth());

    ValueMAP[V] = new z3::expr(VExpr);

    VarMap[VarStr] = ValueMAP[V];

    Var++;
  }

  // Loop over BinOps
  z3::expr *LastInst = nullptr;
  for (auto E = AST.rbegin(); E != AST.rend(); ++E) {
    auto CurInst = E->I;

    // Take the real bitwidth
    // Remove this and parameter
    OverrideBitWidth = CurInst->getType()->getIntegerBitWidth();

    auto BO = dyn_cast<BinaryOperator>(CurInst);
    if (BO) {
      switch (BO->getOpcode()) {
      case Instruction::BinaryOps::Add: {
        auto exp =
            *getZ3Val(Z3Ctx, BO->getOperand(0), ValueMAP, OverrideBitWidth) +
            *getZ3Val(Z3Ctx, BO->getOperand(1), ValueMAP, OverrideBitWidth);
        ValueMAP[BO] = new z3::expr(exp);
      } break;
      case Instruction::BinaryOps::Sub: {
        auto exp =
            *getZ3Val(Z3Ctx, BO->getOperand(0), ValueMAP, OverrideBitWidth) -
            *getZ3Val(Z3Ctx, BO->getOperand(1), ValueMAP, OverrideBitWidth);
        ValueMAP[BO] = new z3::expr(exp);
      } break;
      case Instruction::BinaryOps::Mul: {
        auto exp =
            *getZ3Val(Z3Ctx, BO->getOperand(0), ValueMAP, OverrideBitWidth) *
            *getZ3Val(Z3Ctx, BO->getOperand(1), ValueMAP, OverrideBitWidth);
        ValueMAP[BO] = new z3::expr(exp);
      } break;
      case Instruction::BinaryOps::SDiv: {
        auto exp =
            *getZ3Val(Z3Ctx, BO->getOperand(0), ValueMAP, OverrideBitWidth) /
            *getZ3Val(Z3Ctx, BO->getOperand(1), ValueMAP, OverrideBitWidth);
        ValueMAP[BO] = new z3::expr(exp);
      } break;
      case Instruction::BinaryOps::Xor: {
        auto exp =
            *getZ3Val(Z3Ctx, BO->getOperand(0), ValueMAP, OverrideBitWidth) ^
            *getZ3Val(Z3Ctx, BO->getOperand(1), ValueMAP, OverrideBitWidth);
        ValueMAP[BO] = new z3::expr(exp);
      } break;
      case Instruction::BinaryOps::And: {
        auto exp =
            *getZ3Val(Z3Ctx, BO->getOperand(0), ValueMAP, OverrideBitWidth) &
            *getZ3Val(Z3Ctx, BO->getOperand(1), ValueMAP, OverrideBitWidth);
        ValueMAP[BO] = new z3::expr(exp);
      } break;
      case Instruction::BinaryOps::Or: {
        auto exp =
            *getZ3Val(Z3Ctx, BO->getOperand(0), ValueMAP, OverrideBitWidth) |
            *getZ3Val(Z3Ctx, BO->getOperand(1), ValueMAP, OverrideBitWidth);
        ValueMAP[BO] = new z3::expr(exp);
      } break;
      case Instruction::BinaryOps::Shl: {
        auto exp = z3::shl(
            *getZ3Val(Z3Ctx, BO->getOperand(0), ValueMAP, OverrideBitWidth),
            *getZ3Val(Z3Ctx, BO->getOperand(1), ValueMAP, OverrideBitWidth));
        ValueMAP[BO] = new z3::expr(exp);
      } break;
      case Instruction::BinaryOps::LShr: {
        auto exp = z3::lshr(
            *getZ3Val(Z3Ctx, BO->getOperand(0), ValueMAP, OverrideBitWidth),
            *getZ3Val(Z3Ctx, BO->getOperand(1), ValueMAP, OverrideBitWidth));
        ValueMAP[BO] = new z3::expr(exp);
      } break;
      case Instruction::BinaryOps::AShr: {
        auto exp = z3::ashr(
            *getZ3Val(Z3Ctx, BO->getOperand(0), ValueMAP, OverrideBitWidth),
            *getZ3Val(Z3Ctx, BO->getOperand(1), ValueMAP, OverrideBitWidth));
        ValueMAP[BO] = new z3::expr(exp);
      } break;
      default: {
        BO->print(outs());
        report_fatal_error("Unknown opcode!");
      }
      }

    } else if (auto Trunc = dyn_cast<llvm::TruncInst>(CurInst)) {
      auto exp =
          getZ3Val(Z3Ctx, Trunc->getOperand(0), ValueMAP, OverrideBitWidth)
              ->extract(Trunc->getType()->getIntegerBitWidth() - 1, 0);

      ValueMAP[Trunc] = new z3::expr(exp);
    } else if (auto ZExt = dyn_cast<ZExtInst>(CurInst)) {
      auto exp = z3::zext(
          *getZ3Val(Z3Ctx, ZExt->getOperand(0), ValueMAP, OverrideBitWidth),
          ZExt->getType()->getIntegerBitWidth() -
              ZExt->getOperand(0)->getType()->getIntegerBitWidth());
      ValueMAP[ZExt] = new z3::expr(exp);
    } else if (auto SExt = dyn_cast<SExtInst>(CurInst)) {
      auto exp = z3::sext(
          *getZ3Val(Z3Ctx, SExt->getOperand(0), ValueMAP, OverrideBitWidth),
          SExt->getType()->getIntegerBitWidth() -
              SExt->getOperand(0)->getType()->getIntegerBitWidth());
      ValueMAP[SExt] = new z3::expr(exp);
    } else {
      CurInst->dump();
      report_fatal_error("[getZ3ExpressionFromAST] Unsupported instruction!");
    }

    // Set last inst
    LastInst = ValueMAP[CurInst];
  }

  z3::expr Result = *LastInst;

  // Clean up
  for (auto V : ValueMAP) {
    // Skip Vars
    bool Found = false;
    for (auto &E : VarMap) {
      if (E.second == V.second) {
        Found = true;
        break;
      }
    }

    if (Found)
      continue;

    delete V.second;
  }

  return Result;
}

z3::expr *
LLVMParser::getZ3Val(z3::context &Z3Ctx, llvm::Value *V,
                     llvm::DenseMap<llvm::Value *, z3::expr *> &ValueMap,
                     int OverrideBitWidth) {
  if (ConstantInt *CV = dyn_cast<ConstantInt>(V)) {
    int BitWidth = 0;
    if (OverrideBitWidth) {
      BitWidth = OverrideBitWidth;
    } else {
      BitWidth = CV->getBitWidth();
    }

    if (CV->isNegative()) {
      auto ConstExpr = Z3Ctx.bv_val(CV->getSExtValue(), BitWidth);
      ValueMap[V] = new z3::expr(ConstExpr);
    } else {

      auto ConstExpr = Z3Ctx.bv_val(CV->getZExtValue(), BitWidth);
      ValueMap[V] = new z3::expr(ConstExpr);
    }

    return ValueMap[V];
  }

  if (ValueMap.count(V) == 0) {
    report_fatal_error("[getZ3Val] Value not found!");
  }

  return ValueMap[V];
}

int LLVMParser::getInstructionCount(llvm::Module *M) {
  int Count = 0;
  for (auto &F : *M) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        Count++;
      }
    }
  }
  return Count;
}

int LLVMParser::getInstructionCountBefore() {
  return this->InstructionCountBefore;
}

int LLVMParser::getInstructionCountAfter() {
  return this->InstructionCountAfter;
}

} // namespace LSiMBA
