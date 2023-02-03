#include "LLVMParser.h"

#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Threading.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/Evaluator.h"

#include <memory>
#include <mutex>
#include <thread>

#include "CSiMBA.h"
#include "ShuttingYard.h"
#include "Simplifier.h"
#include "veque.h"

using namespace llvm;
using namespace std;
using namespace std::chrono;

namespace LSiMBA {

llvm::LLVMContext LLVMParser::Context;

LLVMParser::LLVMParser(const std::string &filename, int BitWidth, bool Signed, bool Parallel,
                       bool Verify, bool OptimizeBefore, bool OptimizeAfter)
    : BitWidth(BitWidth), Signed(Signed), Parallel(Parallel), Verify(Verify),
      OptimizeBefore(OptimizeBefore), OptimizeAfter(OptimizeAfter),
      SP64(filename.length()), TLII(nullptr), TLI(nullptr) {
  if (!this->parse(filename)) {
    llvm::errs() << "[!] Error: Could not parse file " << filename << "\n";
    return;
  }

  // Create evaluator
  this->TLII =
      std::make_unique<TargetLibraryInfoImpl>(Triple(M->getTargetTriple()));
  this->TLI = std::make_unique<TargetLibraryInfo>(*TLII);
  this->Eval = std::make_unique<Evaluator>(M->getDataLayout(), TLI.get());

  this->MaxThreadCount = thread::hardware_concurrency();
}

LLVMParser::~LLVMParser() {}

int LLVMParser::simplify() {
  int Count = this->simplifyModule();

  // Disable as it leads to strange crash
  /*
  if (this->OptimizeAfter) {
    runLLVMOptimizer();
  }
  */

  return Count;
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
                                  std::vector<int64_t> &ResultVector,
                                  uint64_t Modulus, int VNumber, Type *IntType) {

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
    auto v = dyn_cast<ConstantInt>(CIRetVal)->getLimitedValue();

    // Store value mod modulus
    ResultVector.push_back(v % Modulus);

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

  M = llvm::parseIRFile(filename, Err, Context);
  if (!M) {
    llvm::errs() << "[!] Error: " << Err.getMessage() << "\n";
    return false;
  }

  if (this->OptimizeBefore) {
    runLLVMOptimizer();
  }

  return true;
}

int LLVMParser::simplifyModule() {
  // Collect all functions
  std::vector<llvm::Function *> Functions;
  for (auto &F : *M) {
    // Skip simplifed functions
    if (F.getName().startswith("MBA_Simp"))
      continue;

    // Check if any load/stores are in the function
    if (hasLoadStores(F))
      report_fatal_error("[!] Error: Function contains load/stores!");

    Functions.push_back(&F);
  }

  // Walk through all functions
  uint64_t Modulus = pow(2, this->BitWidth);
  outs() << "[+] Simplifying " << Functions.size() << " functions ...\t";
  auto start = high_resolution_clock::now();
  for (auto F : Functions) {
    // Get the terminator
    auto Terminator = getSingleTerminator(*F);

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
    std::vector<int64_t> ResultVector;
    initResultVector(*F, ResultVector, Modulus, VNumber, RetTy);

    // Simpify MBA
    Simplifier S(this->BitWidth, this->Signed, false, VNumber,
                 ResultVector);

    std::string SimpExpr;
    S.simplify(SimpExpr, false, false);

    // Convert simplified expression to LLVM IR
    auto FSimp =
        createLLVMFunction(this->M.get(), RetTy, SimpExpr, VNames, Modulus);

    // Verify if simplification is valid
    if (this->Verify && !this->verify(F, FSimp, Modulus)) {
      llvm::errs() << "[!] Error: Simplification is not valid for function "
                   << F->getName() << "\n";
    }
  }

  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(stop - start);

  outs() << "Done! (" << duration.count() << " ms)\n";

  return Functions.size();
}

bool LLVMParser::verify(llvm::Function *F0, llvm::Function *F1,
                        uint64_t Modulus) {
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

  // Run in parallel when user asked for it
  // WARINING: This is not working yet because LLVM does not support
  // multithreading
  if (this->Parallel && 1 != 1) {
    return this->verify_parallel(F0, F1, Modulus);
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

    int64_t R0 = dyn_cast<ConstantInt>(RetVal0)->getSExtValue() % Modulus;
    int64_t R1 = dyn_cast<ConstantInt>(RetVal1)->getSExtValue() % Modulus;

    if (R0 != R1) {
      return false;
    }

    par.clear();
  }

  return true;
}

bool LLVMParser::verify_parallel(llvm::Function *F0, llvm::Function *F1,
                                 uint64_t Modulus) {
  bool IsValid = true;

  auto RetTy = F0->getReturnType();
  auto vnumber = F0->arg_size();

  int CurThreadCount = 0;
  veque::veque<thread> threads;

  mutex m;

  auto fcomp = [&](llvm::Function *F0, llvm::Function *F1) -> void {
    llvm::SmallVector<Constant *, 16> par;

    m.lock();
    auto RetVal0 = ConstantInt::get(RetTy, 0);
    auto RetVal1 = ConstantInt::get(RetTy, 0);

    for (int j = 0; j < vnumber; j++) {
      auto C = ConstantInt::get(RetTy, SP64.next());
      par.push_back(C);
    }
    m.unlock();

    Eval->EvaluateFunction(F0, RetVal0, par);
    Eval->EvaluateFunction(F1, RetVal1, par);

    m.lock();
    int64_t R0 = dyn_cast<ConstantInt>(RetVal0)->getSExtValue() % Modulus;
    int64_t R1 = dyn_cast<ConstantInt>(RetVal1)->getSExtValue() % Modulus;
    if (R0 != R1) {
      IsValid = false;
    }

    m.unlock();
  };

  // Run in parallel
  for (int i = 0; i < NUM_TEST_CASES; i++) {
    threads.push_back(thread(fcomp, F0, F1));

    CurThreadCount++;

    // Wait for one thread to finish
    if (CurThreadCount == this->MaxThreadCount) {
      threads.front().join();
      threads.pop_front();

      --CurThreadCount;
    }

    if (!IsValid)
      break;
  }

  // Wait for threads to finish
  for (auto &t : threads) {
    if (t.joinable())
      t.join();
  }

  return true;
}

bool LLVMParser::runLLVMOptimizer() {
  llvm::legacy::PassManager module_manager;

  outs() << "[+] Running LLVM optimizer ...\t\t";

  llvm::PassManagerBuilder builder;
  builder.OptLevel = 3;
  builder.SizeLevel = 2;
  builder.LibraryInfo = TLII.get();
  builder.DisableUnrollLoops = true;
  builder.MergeFunctions = false;
  builder.RerollLoops = true;
  builder.VerifyInput = false;
  builder.VerifyOutput = false;

  builder.populateModulePassManager(module_manager);

  auto start = high_resolution_clock::now();

  module_manager.run(*this->M.get());

  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(stop - start);

  outs() << " Done! (" << duration.count() << " ms)\n";

  return true;
}

} // namespace LSiMBA
