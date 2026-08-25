#include "LLVMParser.h"

#include <functional>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/Threading.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Evaluator.h"

// add new pass manager builder
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"

#include <cmath>
#include <memory>
#include <stack>
#include <string>
#include <thread>

#if defined(_WIN32) && defined(_MSC_VER)
#define SIMBA_HAVE_SEH 1
#include <windows.h>
#endif

#include <llvm/IR/ConstantFold.h>
#include <z3++.h>
#include <vector>

#include "CSiMBA.h"
#include "Modulo.h"
#include "ShuttingYard.h"
#include "Simplifier.h"
#include "SimplifierRouter.h"
#include "Z3Prover.h"

// #define DEBUG_SIMPLIFICATION

// GCC builtins used in the intrinsic evaluation below are not available on
// plain MSVC; provide portable equivalents (clang-cl understands the GCC
// spellings natively, so this is skipped under clang-cl). These run only in
// the rare constant-evaluation paths, so portability beats intrinsic speed.
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
static inline int __builtin_popcount(uint64_t x) {
  int c = 0;
  while (x) {
    x &= x - 1;
    c++;
  }
  return c;
}
static inline uint16_t __builtin_bswap16(uint16_t x) {
  return _byteswap_ushort(x);
}
static inline uint32_t __builtin_bswap32(uint32_t x) {
  return _byteswap_ulong(x);
}
static inline uint64_t __builtin_bswap64(uint64_t x) {
  return _byteswap_uint64(x);
}
static inline long long __builtin_abs(long long x) {
  return x < 0 ? -x : x;
}
static inline int __builtin_ctz(uint64_t x) {
  if (x == 0)
    return 0; // LLVM ctz convention: 0 for x == 0
  int c = 0;
  while ((x & 1) == 0) {
    x >>= 1;
    c++;
  }
  return c;
}
#endif

using namespace llvm;
using namespace std;
using namespace std::chrono;

// Global z3 context to speed to things
z3::context *Z3CtxGlobal = nullptr;

// Z3CtxGlobal is created once, lazily, on the first LLVMParser
// construction and then shared for the rest of the process - LLVMParser
// itself is constructed fresh per function (see SiMBAPass::run), so its
// lifetime is much shorter than the context's on purpose.
//
// Recreating the context per LLVMParser was tried, as a supposed hygiene
// improvement, while chasing a reproducible crash on SiMBA-heavy targets
// (denuvomaximum): an access violation deep inside libz3, first in
// Z3_inc_ref and ultimately in Z3_del_context. It was in fact the *cause*
// of that crash. prove() (Z3Prover.cpp) caches one solver for the whole
// process, built from the context of the first conjecture it sees;
// freeing that context out from under it left the cached solver pointing
// into freed memory, and the next prove() corrupted Z3's heap through it.
// So: one context, created once, and any code that does tear it down has
// to drop the cached solver first (see abandonZ3CtxGlobal below, and
// resetZ3Solver in Z3Prover.h).
static void ensureZ3CtxGlobal() {
  if (!Z3CtxGlobal) {
    Z3CtxGlobal = new z3::context;
  }
}

// Recovery path for proveWithZ3Guarded only: a hardware fault has come out
// of libz3, so the context's internal state is no longer trustworthy and
// gets replaced. Note what this deliberately does *not* do - it never runs
// Z3's own destructors over the abandoned context. Walking a heap Z3 has
// already corrupted is what turned the fault into a whole-process crash
// (Z3_del_context faulting from inside the exception handler). The old
// context is leaked instead; a fault here is rare and terminal-ish enough
// that leaking it is much the better trade.
static void abandonZ3CtxGlobal() {
  abandonZ3Solver();
  Z3CtxGlobal = new z3::context;
}

cl::OptionCategory SiMBAOpt("SiMBA++ Options");

llvm::cl::opt<std::string> UseExternalSimplifier(
    "external-simplifier", cl::Optional,
    cl::desc("Path to external simplifier script for "
             "simplification (Supports: SiMBA/GAMBA)"),
    cl::value_desc("external-simplifier"), cl::init(""), cl::cat(SiMBAOpt));

llvm::cl::opt<int> MaxVarCount(
    "max-var-count", cl::Optional,
    cl::desc("Max variable count for simplification"),
    cl::value_desc("max-var-count"), cl::init(6), cl::cat(SiMBAOpt));

llvm::cl::opt<int> MinASTSize("min-ast-size", cl::Optional,
                              cl::desc("Minimum AST size for simplification"),
                              cl::value_desc("min-ast-size"), cl::init(3),
                              cl::cat(SiMBAOpt));

llvm::cl::opt<bool> ShouldWalkSubAST(
    "walk-sub-ast", cl::Optional,
    cl::desc("Walk sub AST if full AST does not match"),
    cl::value_desc("walk-sub-ast"), cl::init(false), cl::cat(SiMBAOpt));

llvm::cl::opt<int> MaxMBAGlobal(
    "max-mbas", cl::Optional,
    cl::desc("Option to stop simplification after "
             "a certain amount of MBAs (For debugging)"),
    cl::value_desc("-max-mbas"), cl::init(0), cl::cat(SiMBAOpt));

namespace LSiMBA {

// Defined below (before getASTAsString); used earlier in the routing path.
static bool ASTContainsArithmeticShift(llvm::SmallVectorImpl<BFSEntry> &AST);

// Defined below (before getASTAsString); used earlier in the routing path.
static bool ASTHasUnrenderableInstruction(llvm::SmallVectorImpl<BFSEntry> &AST);

int MBACountStats = 0;

llvm::MapVector<uint64_t, bool> MBACache;

llvm::LLVMContext LLVMParser::Context;

LLVMParser::LLVMParser(const std::string &filename,
                       const std::string &OutputFile, bool Parallel,
                       bool Verify, bool OptimizeBefore, bool OptimizeAfter,
                       bool Debug, bool Prove)
    : OutputFile(OutputFile),
      Parallel(Parallel),
      Verify(Verify),
      OptimizeBefore(OptimizeBefore),
      OptimizeAfter(OptimizeAfter),
      Debug(Debug),
      Prove(Prove),
      SP64(filename.length()),
      TLII(nullptr),
      TLI(nullptr),
      M(nullptr),
      F(nullptr) {
  if (!this->parse(filename)) {
    llvm::errs() << "[!] Error: Could not parse file " << filename << "\n";
    return;
  }

  // Create evaluator
  this->TLII = new TargetLibraryInfoImpl(Triple(M->getTargetTriple()));
  this->TLI = std::make_unique<TargetLibraryInfo>(*TLII);
  this->Eval = std::make_unique<Evaluator>(M->getDataLayout(), TLI.get());

  this->MaxThreadCount = thread::hardware_concurrency();

  this->IsExternalSimplifier = !UseExternalSimplifier.empty();

  // Create the shared z3 context if this is the first parser - see
  // ensureZ3CtxGlobal's own comment for why it is created once and shared
  // rather than recreated per LLVMParser.
  ensureZ3CtxGlobal();
}

LLVMParser::LLVMParser(llvm::Module *M, bool Parallel, bool Verify,
                       bool OptimizeBefore, bool OptimizeAfter, bool Debug,
                       bool Prove)
    : M(M),
      F(nullptr),
      Parallel(Parallel),
      Verify(Verify),
      OptimizeBefore(OptimizeBefore),
      OptimizeAfter(OptimizeAfter),
      Debug(Debug),
      Prove(Prove),
      SP64((uint64_t)M),
      TLII(nullptr),
      TLI(nullptr) {
  // Create evaluator

  this->TLII = new TargetLibraryInfoImpl(Triple(M->getTargetTriple()));
  this->TLI = std::make_unique<TargetLibraryInfo>(*TLII);
  this->Eval = std::make_unique<Evaluator>(M->getDataLayout(), TLI.get());

  this->MaxThreadCount = thread::hardware_concurrency();

  this->IsExternalSimplifier = !UseExternalSimplifier.empty();

  // Create the shared z3 context if this is the first parser - see
  // ensureZ3CtxGlobal's own comment for why it is created once and shared
  // rather than recreated per LLVMParser.
  ensureZ3CtxGlobal();
}

LLVMParser::LLVMParser(llvm::Function *F, bool Parallel, bool Verify,
                       bool OptimizeBefore, bool OptimizeAfter, bool Debug,
                       bool Prove)
    : M(F->getParent()),
      F(F),
      Parallel(Parallel),
      Verify(Verify),
      OptimizeBefore(OptimizeBefore),
      OptimizeAfter(OptimizeAfter),
      Debug(Debug),
      Prove(Prove),
      SP64((uint64_t)M),
      TLII(nullptr),
      TLI(nullptr) {
  // Create evaluator

  this->TLII = new TargetLibraryInfoImpl(Triple(M->getTargetTriple()));
  this->TLI = std::make_unique<TargetLibraryInfo>(*TLII);
  this->Eval = std::make_unique<Evaluator>(M->getDataLayout(), TLI.get());

  this->MaxThreadCount = thread::hardware_concurrency();

  this->IsExternalSimplifier = !UseExternalSimplifier.empty();

  // Disable instruction count as it has a big performance impact
  this->CountInstructions = false;

  // Create the shared z3 context if this is the first parser - see
  // ensureZ3CtxGlobal's own comment for why it is created once and shared
  // rather than recreated per LLVMParser.
  ensureZ3CtxGlobal();
}

LLVMParser::~LLVMParser() {}

int LLVMParser::simplify() {
  // Simplify MBAs
  auto Count = this->extractAndSimplify();

  if (this->CountInstructions) {
    this->InstructionCountAfter = getInstructionCount(M);
  }

  writeModule();

  return Count;
}

int LLVMParser::simplifyMBAFunctionsOnly() {
  int Count = this->simplifyMBAModule();

  writeModule();

  return Count;
}

void LLVMParser::writeModule() {
  if (this->OutputFile.empty()) return;

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
    Eval->EvaluateFunction(&F, RetVal, par);

    // Get Result and store in result vector
    auto CIRetVal = dyn_cast<ConstantInt>(RetVal);
    if (!CIRetVal) {
      // Evaluation produced a non-integer result (e.g. udiv/urem by zero is
      // undefined and the Evaluator returns null) - use 0 for this combo.
      ResultVector.push_back(APInt(IntType->getIntegerBitWidth(), 0));
      par.clear();
      continue;
    }
    APInt v = CIRetVal->getValue();
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

  if (this->CountInstructions) {
    this->InstructionCountBefore = getInstructionCount(M);
  }

  return true;
}

// ---------------------------------------------------------------------------
// Targeted IR rewrite: the "null-byte" roundabout.
//
// Some lifted code tests whether a byte is zero (a C-string terminator) with a
// 5-instruction roundabout instead of a single compare:
//
//   %a = zext i8 %byte to i64
//   %b = shl  nuw i64 %a, 56
//   %c = ashr exact i64 %b, 32
//   %d = and  i64 %c, 0x00FFFFFFFF000000
//   %e = icmp eq i64 %d, 0          ;  <- root (i1)
//
// The roundabout is value-equivalent to `icmp eq i8 %byte, 0` (verified for all
// 256 byte values). The existing string-candidate flow cannot produce this:
// the AST root is an icmp (i1) so candidates are evaluated at BitWidth=1, which
// can only express functions of bit 0 of the byte, whereas `byte == 0` depends
// on all 8 bits. So we rewrite the exact roundabout structure directly here.
// ---------------------------------------------------------------------------
static bool rewriteNullByteRoundabout(llvm::Instruction *Root, bool Debug) {
  // %e = icmp eq i64 %d, 0
  auto *Icmp = llvm::dyn_cast<llvm::ICmpInst>(Root);
  if (!Icmp || Icmp->getPredicate() != llvm::ICmpInst::ICMP_EQ) return false;
  if (!Icmp->getOperand(0)->getType()->isIntegerTy()) return false;
  auto *Zero = llvm::dyn_cast<llvm::ConstantInt>(Icmp->getOperand(1));
  if (!Zero || !Zero->isZero()) return false;
  if (Icmp->getOperand(0)->getType()->getIntegerBitWidth() != 64) return false;

  // %d = and i64 %c, 0x00FFFFFFFF000000  (and is commutative: either operand)
  auto *And = llvm::dyn_cast<llvm::BinaryOperator>(Icmp->getOperand(0));
  if (!And || And->getOpcode() != llvm::Instruction::And) return false;
  const uint64_t kMask = 0x00FFFFFFFF000000ULL;
  llvm::Value *AshrSrc = nullptr;
  for (int i = 0; i < 2; i++) {
    auto *C = llvm::dyn_cast<llvm::ConstantInt>(And->getOperand(i));
    if (C && C->getZExtValue() == kMask) {
      AshrSrc = And->getOperand(1 - i);
      break;
    }
  }
  if (!AshrSrc) return false;

  // %c = ashr exact i64 %b, 32
  auto *Ashr = llvm::dyn_cast<llvm::BinaryOperator>(AshrSrc);
  if (!Ashr || Ashr->getOpcode() != llvm::Instruction::AShr) return false;
  auto *AshrAmt = llvm::dyn_cast<llvm::ConstantInt>(Ashr->getOperand(1));
  if (!AshrAmt || AshrAmt->getZExtValue() != 32) return false;
  llvm::Value *ShlSrc = Ashr->getOperand(0);

  // %b = shl nuw i64 %a, 56
  auto *Shl = llvm::dyn_cast<llvm::BinaryOperator>(ShlSrc);
  if (!Shl || Shl->getOpcode() != llvm::Instruction::Shl) return false;
  auto *ShlAmt = llvm::dyn_cast<llvm::ConstantInt>(Shl->getOperand(1));
  if (!ShlAmt || ShlAmt->getZExtValue() != 56) return false;
  llvm::Value *ZextSrc = Shl->getOperand(0);

  // %a = zext i8 %byte to i64
  auto *Zext = llvm::dyn_cast<llvm::ZExtInst>(ZextSrc);
  if (!Zext) return false;
  if (Zext->getOperand(0)->getType()->getIntegerBitWidth() != 8) return false;
  llvm::Value *Byte = Zext->getOperand(0);

  // Emit `icmp eq i8 %byte, 0` in place of the roundabout root.
  auto *NewIcmp = new llvm::ICmpInst(
      Root->getIterator(), llvm::ICmpInst::ICMP_EQ, Byte,
      llvm::ConstantInt::get(llvm::Type::getInt8Ty(Root->getContext()), 0));
  Root->replaceAllUsesWith(NewIcmp);

  // Erase the now-dead roundabout instructions (only if they have no other
  // uses, so a shared value is never touched).
  llvm::Instruction *Chain[] = {Root, And, Ashr, Shl, Zext};
  for (llvm::Instruction *I : Chain) {
    if (I && !I->use_empty()) continue;
    I->eraseFromParent();
  }

  if (Debug) {
    outs() << "[!] Rewrote null-byte roundabout to `icmp eq i8 %x, 0`\n";
  }
  return true;
}

static int rewriteNullByteRoundabouts(llvm::Function &F, bool Debug) {
  // Collect candidate roots up front (we may erase instructions below).
  llvm::SmallVector<llvm::Instruction *, 8> Roots;
  for (auto &BB : F)
    for (auto &I : BB)
      if (auto *IC = llvm::dyn_cast<llvm::ICmpInst>(&I))
        if (IC->getPredicate() == llvm::ICmpInst::ICMP_EQ) Roots.push_back(&I);

  int Count = 0;
  for (auto *R : Roots) {
    if (rewriteNullByteRoundabout(R, Debug)) Count++;
  }
  return Count;
}

int LLVMParser::extractAndSimplify() {
  int MBASimplified = 0;

  if (MaxMBAGlobal && MBACountStats >= MaxMBAGlobal) return 0;

  // Collect all functions
  std::vector<llvm::Function *> Functions;
  for (auto &F : *M) {
    // If F set only work on F
    if (this->F && (&F != this->F)) {
      continue;
    }

    if (F.isDeclaration()) {
      continue;
    }

    // Skip simplifed functions
    if (F.getName().starts_with("MBA_Simp")) {
      continue;
    }

    Functions.push_back(&F);
  }

  // Walk through all functions
  /*
  if (this->Debug) {
    outs() << "[+] Simplifying " << Functions.size() << " function(s) ...\n";
  }
  */

  auto start = high_resolution_clock::now();
  for (auto F : Functions) {
    if (F->isDeclaration()) continue;

    if (F->getName().contains("_keep")) {
      if (this->Debug) {
        outs() << "[!] Skipping simplification of function: " << F->getName()
               << "\n";
      }
      continue;
    }

    /*
    if (this->Debug) {
      outs() << "[*] Simplifying function: " << F->getName() << "\n";
    }
    */

    // Optimize before if asked for
    if (this->OptimizeBefore) {
      optimizeFunction(*F);
    }

    int MBACount = 0;
    bool Found = false;

    DominatorTree DT(*F);

    // Measure Time
    auto start = high_resolution_clock::now();

    // Get candidates
    std::vector<MBACandidate> Candidates;
    this->extractCandidates(*F, Candidates);

    // Find valid replacements for candidates
    Found = this->findReplacements(&DT, Candidates);

    // Apply replacements and optimize
    bool Replaced = false;
    for (int i = 0; i < Candidates.size(); i++) {
      if (Candidates[i].isValid == false) continue;

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

      // Dead-code elimination: the replacement redirected the root's uses to a
      // freshly built expression, so some of the old AST instructions may now
      // have no remaining uses. Erase them (repeat until fixpoint, since
      // erasing one instruction can make another unused). Only instructions
      // from this candidate's AST are considered, so a shared value that is
      // still used elsewhere is never touched.
      {
        llvm::SmallVector<llvm::Instruction *, 32> ASTInsts;
        for (auto &E : Candidates[i].AST) ASTInsts.push_back(E.I);
        // Track erased pointers so we never dereference a dangling one (the
        // AST may list an instruction more than once, and eraseFromParent
        // invalidates the pointer).
        llvm::DenseSet<llvm::Instruction *> ErasedSet;
        bool Erased = true;
        while (Erased) {
          Erased = false;
          for (auto *I : ASTInsts) {
            if (!I || ErasedSet.count(I)) continue;
            if (!I->use_empty()) continue;  // still used; keep it
            if (this->Debug) {
              I->printAsOperand(outs(), false);
              outs() << " erased (no remaining uses)\n";
            }
            I->eraseFromParent();
            ErasedSet.insert(I);
            Erased = true;
          }
        }
      }

      MBASimplified++;
      MBACount++;

      Replaced = true;

      // Global Stats
      MBACountStats++;
    }

    // Targeted rewrite: shorten the "null-byte" roundabout (zext/shl 56/ashr
    // 32/and 0x00FFFFFFFF000000/icmp eq 0) to `icmp eq i8 %byte, 0`. Runs after
    // the string-candidate flow, which preserves these checks (a 1-bit
    // candidate cannot express `byte == 0`).
    rewriteNullByteRoundabouts(*F, this->Debug);

    // Optimize if any replacements
    if (Replaced && this->OptimizeAfter) {
      optimizeFunction(*F);
    }
  }

  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(stop - start);
  if (this->Debug && MBASimplified) {
    outs() << "[" << MBACountStats << "] Done! " << MBASimplified
           << " MBAs simplified (" << duration.count() << " ms)\n";
  }

  return MBASimplified;
}

int LLVMParser::simplifyMBAModule() {
  // Collect all functions
  std::vector<llvm::Function *> Functions;
  for (auto &F : *M) {
    if (this->F && (&F != this->F)) continue;

    // Skip simplifed functions
    if (F.getName().starts_with("MBA_Simp")) continue;

    // Check if any load/stores are in the function
    if (hasLoadStores(F))
      report_fatal_error("[!] Error: Function contains load/stores!");

    Functions.push_back(&F);
  }

  // Walk through all functions
  outs() << "[+] Simplifying " << Functions.size() << " functions ...\t";

  auto start = high_resolution_clock::now();
  for (auto F : Functions) {
    // Optimize before if asked for
    if (this->OptimizeBefore) {
      optimizeFunction(*F);
    }

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

    // Simplify MBA
    Simplifier S(BitWidth, false, VNumber, ResultVector);

    std::string SimpExpr;

    // Phase 9: route to the selected simplifier (--simplifier). The native
    // selection (default) keeps the original path below untouched.
    bool Routed = false;
    std::vector<std::string> RoutedVNames = VNames;
    {
      DominatorTree DT(*F);
      SmallVector<BFSEntry, 16> AST;
      SmallVector<llvm::Value *, 8> RoutedVars;
      // The terminator is a `ret`; the real expression root is its operand.
      // Pass that root with KeepRoot=true (matching the `simplify` path) so the
      // AST contains the outermost operator and getASTAsString renders it.
      if (auto Root = dyn_cast<Instruction>(Terminator->getOperand(0)))
        this->getAST(&DT, Root, AST, RoutedVars, true);

      if (!RoutedVars.empty()) {
        // Reorder the used variables into the original declaration order so
        // the letter mapping is deterministic ('a' = first declared used
        // argument, ...).
        SmallVector<llvm::Value *, 8> OrderedVars;
        for (auto &V : Variables)
          for (auto &RV : RoutedVars)
            if (RV == V)
              OrderedVars.push_back(RV);

        auto Expr = this->getASTAsString(AST, OrderedVars);

        std::string RoutedRepl;
        Routed = LSiMBA::TrySelectedSimplifier(Expr, RoutedRepl, BitWidth,
                                               this->Prove,
                                               LSiMBA::autoFallbackEnabled());
        if (Routed) {
          SimpExpr = RoutedRepl;

          // The routed expression's letters refer to OrderedVars; rebuild the
          // name mapping so the j-th declared argument gets the letter the
          // expression uses for it. Unused arguments get letters beyond the
          // used range so they never collide with the expression's letters.
          RoutedVNames.assign(VNames.size(), "");
          int UnusedIdx = 0;
          for (size_t j = 0; j < VNames.size(); j++) {
            char Letter = 0;
            for (size_t i = 0; i < OrderedVars.size(); i++) {
              if (OrderedVars[i] == Variables[j]) {
                Letter = 'a' + i;
                break;
              }
            }
            if (Letter)
              RoutedVNames[j] = std::string(1, Letter);
            else
              RoutedVNames[j] = std::string(
                  1, 'a' + OrderedVars.size() + UnusedIdx++);
          }
        }
      }
    }

    if (!Routed) {
      S.simplify(SimpExpr, false, false);
    }

    // Convert simplified expression to LLVM IR
    auto FSimp = createLLVMFunction(
        this->M, Variables, SimpExpr, RoutedVNames, RetTy);

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

int LLVMParser::countVariables(std::string &expr, char Var) {
  int VarCount = 0;
  for (int j = 0; j < expr.size(); j++) {
    if (expr[j] == Var) {
      VarCount++;
    }
  }

  return VarCount;
}

// Runs the actual Z3-proving work for one candidate. Deliberately its own,
// small function containing the only C++ objects with destructors
// involved (Z3ExpOpt, the OPTSTATUS local) - proveWithZ3Guarded's __try
// block below calls this rather than doing the work inline, since mixing
// SEH __try/__except directly with such objects in the same function is
// unsupported by MSVC/clang-cl. See proveWithZ3Guarded for why this
// exists at all.
void doProveWithZ3(LLVMParser *Self, std::string &SimpExpr,
                   std::vector<std::string> &Vars,
                   llvm::SmallVectorImpl<BFSEntry> &AST,
                   llvm::SmallVectorImpl<llvm::Value *> &Variables,
                   bool &Result) {
  OPTSTATUS Proved;
  auto Z3ExpOpt = Self->getOptimizedZ3Expression(*Z3CtxGlobal, SimpExpr, Vars,
                                                 AST, Variables, Proved);

  if (Proved == OPT_PROVED) {
    Result = true;
  } else if (Proved == OPT_NOT_VALID) {
    Result = false;
  } else if (OPT_PROVE_ME) {
    // prove() is a free function (declared in Z3Prover.h), not a member -
    // matches how verify() itself originally called it, unqualified.
    Result = prove((Z3ExpOpt != 0));
  }
}

// Last-resort net around the Z3-proving call. The specific crash this was
// originally written for turned out to be a use-after-free on our side -
// the cached global solver outliving its context, see ensureZ3CtxGlobal -
// and is fixed at the source, so this should now never fire. It is kept
// because a fault escaping libz3 is a hardware exception (SEH), not a
// catchable z3::exception, so without it *any* such fault takes the whole
// lift down mid-run; with it, one MBA candidate is treated as not proved,
// which is an outcome verify() already handles as normal (OPT_NOT_VALID).
//
// It reports unconditionally rather than only under Debug: a fault
// reaching here means something is genuinely wrong, and silently
// swallowing it is how the underlying bug stayed hidden.
static bool proveWithZ3Guarded(LLVMParser *Self, std::string &SimpExpr,
                               std::vector<std::string> &Vars,
                               llvm::SmallVectorImpl<BFSEntry> &AST,
                               llvm::SmallVectorImpl<llvm::Value *> &Variables,
                               bool Debug) {
  bool Result = false;
#if defined(SIMBA_HAVE_SEH)
  __try {
    doProveWithZ3(Self, SimpExpr, Vars, AST, Variables, Result);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    errs() << "[Z3] Proving faulted with a hardware exception - treating "
              "this candidate as not proved and replacing the shared Z3 "
              "context. This should not happen; please report it.\n";
    abandonZ3CtxGlobal();
    Result = false;
  }
#else
  doProveWithZ3(Self, SimpExpr, Vars, AST, Variables, Result);
#endif
  return Result;
}

bool LLVMParser::verify(int ASTSize, llvm::SmallVectorImpl<BFSEntry> &AST,
                        std::string &SimpExpr,
                        llvm::SmallVectorImpl<llvm::Value *> &Variables,
                        int BitWidth, bool DoZ3) {
  int VNumber = Variables.size();
  // int BitWidth = AST.front().I->getType()->getIntegerBitWidth();
  auto Modulus = getModulus(BitWidth);

  // Check Ptr is used several times
  for (int i = 0; i < Variables.size(); i++) {
    if (Variables[i]->getType()->isPointerTy()) {
      char c = 'a' + i;

      // Count vars in expr
      int Count = countVariables(SimpExpr, c);
      if (Count > 1) {
        return false;
      }

      // Count the operations
      int OpCount = countOperators(SimpExpr);
      if (OpCount > 1) {
        return false;
      }
    }
  }

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
  for (int i = 0; i < NUM_TEST_CASES; i++) {
    for (int j = 0; j < VNumber; j++) {
      auto v = SP64.next();
      // Assign each opaque variable its FULL actual type width, not just
      // BitWidth bits. BitWidth is the AST *root* width (e.g. 1 for an icmp
      // root), but the opaque variable may be wider (e.g. 8-bit for a byte).
      // Assigning only BitWidth bits made the quick test exercise just a tiny
      // slice of the variable's value space, letting value-wrong candidates
      // (that agree on that slice but not on the full range) slip through.
      int VarWidth = 64;
      if (!Variables[j]->getType()->isPointerTy()) {
        int W = Variables[j]->getType()->getIntegerBitWidth();
        if (W > 0 && W <= 64)
          VarWidth = W;
      }
      // Truncate explicitly rather than relying on APInt's implicitTrunc
      // constructor argument - that overload doesn't exist in every LLVM
      // version this needs to build against (e.g. LLVM 18).
      uint64_t Truncated =
          (VarWidth >= 64) ? v : (v & ((uint64_t(1) << VarWidth) - 1));
      par.push_back(APInt(VarWidth, Truncated, false));
    }

    // Eval AST
    bool Error = false;
    auto AP_R0 = this->evaluateAST(AST, Variables, par, Error);
    if (Error) {
      if (this->Debug)
        outs() << "[*] [VERIFY] eval-AST error for '" << SimpExpr << "'\n";
      return false;
    }

    // Eval replacement
    auto AP_R1 = eval(Expr1_replVar, par, BitWidth, &Operations);

    // Check if replacement is cheaper than original expression
    if (ASTSize <= Operations) {
      if (this->Debug)
        outs() << "[*] [VERIFY] no-improve '" << SimpExpr << "' AST=" << ASTSize
               << " Ops=" << Operations << "\n";
      return false;
    }

    if (AP_R0 != AP_R1) {
      if (this->Debug)
        outs() << "[*] [VERIFY] mismatch '" << SimpExpr << "' R0=" << AP_R0
               << " R1=" << AP_R1 << "\n";
      return false;
    }

    par.clear();
  }

#ifdef DEBUG_SIMPLIFICATION
  outs() << "[+] Simplification passed quick test! Running Z3\n";
#endif

  // Prove with z3. DoZ3=false skips this (the quick test above is sufficient
  // for the local MBA identities, which are algebraic laws always true; Z3
  // otherwise spends its 30 s budget and wrongly rejects correct folds).
  if (this->Prove && DoZ3) {
    // Build Variable replacements
    std::vector<std::string> Vars;
    std::map<std::string, llvm::Type *> VarTypes;
    for (int i = 0; i < Variables.size(); i++) {
      char c = 'a' + i;
      string strC = string(1, c);
      Vars.push_back(strC);

      VarTypes[strC] = Variables[i]->getType();
    }

    /*
    if (this->Debug) {
      outs() << "[Z3] Proving ...\n";
    }
    */

    // New way: opt(Exp0 - Exp1) != 0
    // See proveWithZ3Guarded's own comment for why this goes through a
    // dedicated SEH-guarded wrapper rather than calling
    // getOptimizedZ3Expression/prove directly: a genuine hardware
    // exception (not a catchable z3::exception) has been observed to
    // escape from inside libz3 on SiMBA-heavy targets. A plain try/catch
    // around a z3::exception is kept too, for the ordinary Z3 API-level
    // error case check_error/Z3_THROW is actually designed to catch.
    auto start = high_resolution_clock::now();

    bool Result = false;
    try {
      Result = proveWithZ3Guarded(this, SimpExpr, Vars, AST, Variables,
                                  this->Debug);
    } catch (z3::exception &e) {
      if (this->Debug) {
        outs() << "[Z3] Proving threw: " << e.msg() << " - treating as not "
                                                        "proved\n";
      }
      Result = false;
    }

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

bool LLVMParser::isSupportedInstruction(llvm::Value *V) {
  // For new intrinsics check alive2 code for Z3 implementation
  if (auto BO = dyn_cast<BinaryOperator>(V)) {
    // Got removed from constant expr. The division/remainder opcodes were also
    // dropped from ConstantExpr::isSupportedBinOp in recent LLVM, so list them
    // here too; getASTAsString renders them as unsigned '/' and '%'.
    if (BO->getOpcode() == Instruction::Shl ||
        BO->getOpcode() == Instruction::Or ||
        BO->getOpcode() == Instruction::And ||
        BO->getOpcode() == Instruction::LShr ||
        BO->getOpcode() == Instruction::AShr ||
        BO->getOpcode() == Instruction::UDiv ||
        BO->getOpcode() == Instruction::SDiv ||
        BO->getOpcode() == Instruction::URem ||
        BO->getOpcode() == Instruction::SRem) {
      return true;
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

  if (isa<SelectInst>(V)) {
    if (IsExternalSimplifier) return false;

    return true;
  }

  if (isa<ICmpInst>(V)) {
    if (IsExternalSimplifier) return false;

    // Check if operands are pointer type
    auto IC = dyn_cast<ICmpInst>(V);
    if (IC->getOperand(0)->getType()->isPointerTy() ||
        IC->getOperand(1)->getType()->isPointerTy()) {
      return false;
    }

    return true;
  }

  if (auto GEP = dyn_cast<GetElementPtrInst>(V)) {
    // Check if i8 type and only one index
    if (GEP->getNumIndices() != 1) return false;

    if (GEP->getSourceElementType() != Type::getInt8Ty(GEP->getContext()))
      return false;

    // Must be a PtrAdd
    return true;
  }

  if (isa<CallInst>(V)) {
    // check if intrinsic
    auto CI = dyn_cast<CallInst>(V);
    auto Intr = CI->getIntrinsicID();
    switch (Intr) {
      case 0: {
        // Not an intrinsic
        return false;
      }
      case Intrinsic::fshl:
      case Intrinsic::fshr:
      case Intrinsic::ctpop:
      case Intrinsic::bswap:
      case Intrinsic::umax:
      case Intrinsic::umin:
      case Intrinsic::abs:
      case Intrinsic::smin:
      case Intrinsic::smax: {
        return true;
      }
      case Intrinsic::bitreverse: {
        // Check if i8/i16/i32/i64
        auto BW = CI->getArgOperand(0)->getType()->getIntegerBitWidth();
        switch (BW) {
          case 8:
          case 16:
          case 32:
          case 64:
          case 128:
            return true;
          default:
            return false;
        }
      }
      default: {
        outs() << "[!] Unsupported intrinsic: " << "\n";
        CI->dump();
        // report_fatal_error("Unsupported intrinsic");
        // Check SLOT (https://github.com/mikekben/SLOT) for implementations
        return false;
      }
    }
  }

  return false;
}

void LLVMParser::extractCandidates(llvm::Function &F,
                                   std::vector<MBACandidate> &Candidates) {
  // std::set<llvm::Value *> Visited;
  llvm::SmallPtrSet<llvm::Value *, 8> Visited;

  auto isVisited = [&](llvm::Value *I) -> bool {
    // return Visited.find(I) != Visited.end();
    return Visited.count(I);
  };

  // Instruction to look for 'store', 'select', 'gep', 'icmp', 'ret', ...
  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    // Check if integer typ
    if (!I->getType()->isIntegerTy() && !I->getType()->isPointerTy() &&
        !isa<BranchInst>(&*I) && !isa<StoreInst>(&*I) &&
        !isa<ReturnInst>(&*I)) {
      continue;
    }

    switch (I->getOpcode()) {
      case Instruction::Store: {
        // Check Candidate
        auto SI = dyn_cast<StoreInst>(&*I);
        auto Op = SI->getValueOperand();
        if (!isVisited(Op) && isSupportedInstruction(Op)) {
          MBACandidate Cand;
          Cand.Candidate = dyn_cast<Instruction>(Op);
          Candidates.push_back(Cand);
          Visited.insert(Op);
        }

        auto Ptr = SI->getPointerOperand();
        if (!isVisited(Ptr) && isSupportedInstruction(Ptr)) {
          MBACandidate Cand;
          Cand.Candidate = dyn_cast<Instruction>(Ptr);
          Candidates.push_back(Cand);
          Visited.insert(Ptr);
        }
      } break;
      case Instruction::Load: {
        // Check Candidate
        auto LI = dyn_cast<LoadInst>(&*I);
        auto Op = LI->getPointerOperand();
        if (!isVisited(Op) && isSupportedInstruction(Op)) {
          MBACandidate Cand;
          Cand.Candidate = dyn_cast<Instruction>(Op);
          Candidates.push_back(Cand);
          Visited.insert(Op);
        }
      } break;

      case Instruction::GetElementPtr: {
        auto GEP = dyn_cast<GetElementPtrInst>(&*I);
        auto Index = GEP->getOperand(GEP->getNumOperands() - 1);

        // Todo add GEP direclty if its a ptrAdd
        // !!! Disabled for now as it leads to  wrong results!
        /*
        if (!isVisited(GEP)) {
          MBACandidate Cand;
          Cand.Candidate = dyn_cast<Instruction>(GEP);
          Candidates.push_back(Cand);
          Visited.insert(GEP);
        }
        */
        if (isSupportedInstruction(Index)) {
          if (isVisited(Index)) continue;

          MBACandidate Cand;
          Cand.Candidate = dyn_cast<Instruction>(Index);
          Candidates.push_back(Cand);
          Visited.insert(Index);
        }
      } break;
      case Instruction::ICmp: {
        if (IsExternalSimplifier) continue;

        for (unsigned int i = 0; i < I->getNumOperands(); i++) {
          if (isSupportedInstruction(I->getOperand(i)->stripPointerCasts())) {
            if (isVisited(I->getOperand(i)->stripPointerCasts())) continue;
            MBACandidate Cand;
            Cand.Candidate =
                dyn_cast<Instruction>(I->getOperand(i)->stripPointerCasts());
            Candidates.push_back(Cand);
            Visited.insert(I->getOperand(i)->stripPointerCasts());
          }
        }
      } break;
      case Instruction::Ret: {
        auto RI = dyn_cast<ReturnInst>(&*I);
        if (!RI->getReturnValue()) continue;

        if (isSupportedInstruction(RI->getReturnValue()->stripPointerCasts())) {
          if (isVisited(RI->getReturnValue()->stripPointerCasts())) continue;
          MBACandidate Cand;
          Cand.Candidate =
              dyn_cast<Instruction>(RI->getReturnValue()->stripPointerCasts());
          Candidates.push_back(Cand);
          Visited.insert(RI->getReturnValue()->stripPointerCasts());
        }
      } break;
      case Instruction::Call: {
        auto CI = dyn_cast<CallInst>(&*I);
        for (unsigned int i = 0; i < CI->arg_size(); i++) {
          if (isSupportedInstruction(
                  CI->getArgOperand(i)->stripPointerCasts())) {
            if (isVisited(CI->getArgOperand(i)->stripPointerCasts())) continue;

            MBACandidate Cand;
            Cand.Candidate = dyn_cast<Instruction>(
                CI->getArgOperand(i)->stripPointerCasts());
            Candidates.push_back(Cand);
            Visited.insert(CI->getArgOperand(i)->stripPointerCasts());
          }
        }
      } break;
      case Instruction::Br: {
        auto BI = dyn_cast<BranchInst>(&*I);
        if (BI->isConditional()) {
          if (isSupportedInstruction(BI->getCondition())) {
            if (isVisited(BI->getCondition())) continue;
            MBACandidate Cand;
            Cand.Candidate = dyn_cast<Instruction>(BI->getCondition());
            Candidates.push_back(Cand);
            Visited.insert(BI->getCondition());
          }
        }
      } break;
      case Instruction::Select: {
        // Add Instruction
        auto SI = dyn_cast<SelectInst>(&*I);
        if (!isVisited(SI)) {
          MBACandidate Cand;
          Cand.Candidate = dyn_cast<Instruction>(SI);
          Candidates.push_back(Cand);
          Visited.insert(SI);
        }

        // Add Condition
        if (isSupportedInstruction(SI->getCondition()->stripPointerCasts())) {
          if (isVisited(SI->getCondition()->stripPointerCasts())) continue;
          MBACandidate Cand;
          Cand.Candidate =
              dyn_cast<Instruction>(SI->getCondition()->stripPointerCasts());
          Candidates.push_back(Cand);
          Visited.insert(SI->getCondition()->stripPointerCasts());
        }

        // Add Operands
        for (unsigned int i = 0; i < I->getNumOperands(); i++) {
          if (isSupportedInstruction(I->getOperand(i)->stripPointerCasts())) {
            if (isVisited(I->getOperand(i)->stripPointerCasts())) continue;
            MBACandidate Cand;
            Cand.Candidate =
                dyn_cast<Instruction>(I->getOperand(i)->stripPointerCasts());
            Candidates.push_back(Cand);
            Visited.insert(I->getOperand(i)->stripPointerCasts());
          }
        }
      } break;
      case Instruction::PHI: {
        auto Phi = dyn_cast<PHINode>(&*I);
        for (auto &Inc : Phi->incoming_values()) {
          if (isSupportedInstruction(Inc->stripPointerCasts())) {
            if (isVisited(Inc->stripPointerCasts())) continue;
            MBACandidate Cand;
            Cand.Candidate = dyn_cast<Instruction>(Inc->stripPointerCasts());
            Candidates.push_back(Cand);
            Visited.insert(Inc->stripPointerCasts());
          }
        }
      } break;

      case Instruction::Add:
      case Instruction::Sub:
      case Instruction::Mul:
      case Instruction::Shl:
      case Instruction::Xor:
      case Instruction::Trunc:
      case Instruction::Or:
      case Instruction::And:
      case Instruction::URem:
      case Instruction::SRem:
      case Instruction::IntToPtr:
      case Instruction::BitCast: {
        if (isVisited(&*I)) continue;
        MBACandidate Cand;
        Cand.Candidate = dyn_cast<Instruction>(&*I);
        Candidates.push_back(Cand);
        Visited.insert(&*I);
      } break;

      case Instruction::LShr:
      case Instruction::AShr: {
        if (IsExternalSimplifier || isVisited(&*I)) continue;
        MBACandidate Cand;
        Cand.Candidate = dyn_cast<Instruction>(&*I);
        Candidates.push_back(Cand);
        Visited.insert(&*I);
      } break;
      default: {
      }
    }
  }
#ifdef DEBUG_SIMPLIFICATION
  outs() << "[*] Found " << Candidates.size()
         << " candidates Duplicates: " << (Visited.size() - Candidates.size())
         << "\n";
#endif
}

bool LLVMParser::constainsReplacedInstructions(
    SmallPtrSet<llvm::Instruction *, 16> &ReplacedInstructions,
    MBACandidate &Cand) {
  for (auto &E : Cand.AST) {
    if (ReplacedInstructions.find(E.I) != ReplacedInstructions.end()) {
      return true;
    }
  }
  return false;
}

bool LLVMParser::replaceWithKnownPatterns(
    LSiMBA::MBACandidate &Cand, const std::vector<APInt> &ResultVector) {
  const auto &RV = ResultVector;
  size_t N = Cand.Variables.size();
  if (N == 0 || RV.empty())
    return false;

  if (this->Debug) {
    outs() << "[*] [PATTERN] N=" << N << " RVsize=" << RV.size() << " RV=[";
    for (size_t i = 0; i < RV.size() && i < 8; i++) {
      SmallString<16> s;
      RV[i].toString(s, 10, true);
      outs() << (i ? "," : "") << s.str();
    }
    outs() << "] vars:";
    for (auto *V : Cand.Variables) {
      SmallString<16> s;
      raw_svector_ostream os(s);
      V->print(os);
      outs() << " " << s.str();
    }
    outs() << "\n";
  }

  // The ResultVector is a {0,1} truth table: RV[i] has variable j = bit j of i.
  // For 1 var: RV = [f(0), f(1)]. For 2 vars: RV = [f(0,0), f(1,0), f(0,1),
  // f(1,1)]. We match the well-known MBA identities on this table. (Every match
  // is re-verified by the caller via verify()/Z3, so a false positive is safe.)

  if (N == 1 && RV.size() == 2) {
    // ~a  : [~0, ~1] = [allOnes, allOnes-1]
    if (RV[0].isAllOnes() && RV[1] == (RV[0] - 1)) {
      Cand.Replacement = "~a";
      return true;
    }
    // -a  : [0, -1] = [0, allOnes]
    if (RV[0].isZero() && RV[1].isAllOnes()) {
      Cand.Replacement = "-a";
      return true;
    }
    // !a  : [1, 0]
    if (RV[0].getSExtValue() == 1 && RV[1].isZero()) {
      Cand.Replacement = "!a";
      return true;
    }
    // a   : [0, 1]
    if (RV[0].isZero() && RV[1].getSExtValue() == 1) {
      Cand.Replacement = "a";
      return true;
    }
  }

  if (N == 2 && RV.size() == 4) {
    auto eq = [](const APInt &v, long long c) {
      return v.getSExtValue() == c;
    };
    // a+b : [0, 1, 1, 2]
    if (RV[0].isZero() && eq(RV[1], 1) && eq(RV[2], 1) && eq(RV[3], 2)) {
      Cand.Replacement = "a+b";
      return true;
    }
    // a^b : [0, 1, 1, 0]
    if (RV[0].isZero() && eq(RV[1], 1) && eq(RV[2], 1) && RV[3].isZero()) {
      Cand.Replacement = "a^b";
      return true;
    }
    // a-b : [0, 1, -1, 0]  (-1 == allOnes)
    if (RV[0].isZero() && eq(RV[1], 1) && RV[2].isAllOnes() &&
        RV[3].isZero()) {
      Cand.Replacement = "a-b";
      return true;
    }
    // a&b : [0, 0, 0, 1]
    if (RV[0].isZero() && RV[1].isZero() && RV[2].isZero() &&
        eq(RV[3], 1)) {
      Cand.Replacement = "a&b";
      return true;
    }
    // a|b : [0, 1, 1, 1]
    if (RV[0].isZero() && eq(RV[1], 1) && eq(RV[2], 1) && eq(RV[3], 1)) {
      Cand.Replacement = "a|b";
      return true;
    }
  }

  return false;
}

bool LLVMParser::tryCandidateSimplifications(LSiMBA::MBACandidate &Cand,
                                             int BitWidth) {
  // Build the single-letter variable names in the same order the verifier uses
  // (Cand.Variables[0] -> 'a', [1] -> 'b', ...).
  std::vector<std::string> Names;
  char c = 'a';
  for (auto *V : Cand.Variables) {
    Names.push_back(std::string(1, c));
    c++;
  }

  // Propose a small set of simple rewrites. Order matters: cheapest / most
  // common first. Every proposal is re-verified by the caller (verify/Z3), so a
  // false positive is impossible.
  std::vector<std::string> Cands;
  for (auto &n : Names) {
    Cands.push_back(n);
    Cands.push_back("~" + n);
  }
  for (size_t i = 0; i < Names.size(); i++) {
    for (size_t j = i + 1; j < Names.size(); j++) {
      Cands.push_back(Names[i] + "+" + Names[j]);
      Cands.push_back(Names[i] + "-" + Names[j]);
      Cands.push_back(Names[i] + "^" + Names[j]);
      Cands.push_back(Names[i] + "&" + Names[j]);
      Cands.push_back(Names[i] + "|" + Names[j]);
      Cands.push_back(Names[i] + "*" + Names[j]);
    }
  }
  Cands.push_back("0");
  Cands.push_back("-1");

  if (this->Debug) {
    outs() << "[*] [CANDIDATE] === ASTSize=" << Cand.ASTSize << " ===\n";
    printAST(Cand.AST);
  }

  for (auto &cand : Cands) {
    Cand.Replacement = cand;
    bool ok = this->verify(Cand.ASTSize, Cand.AST, Cand.Replacement,
                           Cand.Variables, BitWidth);
    if (this->Debug) {
      outs() << "[*] [CANDIDATE] try '" << cand << "' -> "
             << (ok ? "OK" : "no") << "\n";
    }
    if (ok) {
      return true;
    }
  }
  return false;
}

namespace {
// ---------------------------------------------------------------------------
// Local MBA pattern matching: a small canonical expression tree plus the
// classic mixed boolean-arithmetic identities. Operates on the candidate's
// instruction sequence (intermediate values), so it catches the obfuscated
// 64-bit MBAs the {0,1} truth table cannot. Only the operators that are
// consistent between the quick-test evaluator (eval) and the IR builder
// (createLLVMReplacement) are used: + - ^ & | * < (shl) and unary ~.
// ---------------------------------------------------------------------------
struct MExpr {
  enum Kind { Var, Const, Bin, Not, Cast } K;
  std::string S;   // Var name, Const decimal, or Cast target width
  std::string Op;  // Bin: "+","-","^","&","|","*","<","a"(ashr)
                   // Cast: "sext","zext"
  int SW = 0;      // Cast source width in bits (0 = not a materializable cast)
  std::shared_ptr<MExpr> L, R;
};
using MExprPtr = std::shared_ptr<MExpr>;

MExprPtr mVar(std::string n) {
  auto E = std::make_shared<MExpr>();
  E->K = MExpr::Var;
  E->S = std::move(n);
  return E;
}
MExprPtr mConst(std::string v) {
  auto E = std::make_shared<MExpr>();
  E->K = MExpr::Const;
  E->S = std::move(v);
  return E;
}
MExprPtr mBin(std::string op, MExprPtr l, MExprPtr r) {
  auto E = std::make_shared<MExpr>();
  E->K = MExpr::Bin;
  E->Op = std::move(op);
  E->L = std::move(l);
  E->R = std::move(r);
  return E;
}
MExprPtr mNot(MExprPtr l) {
  auto E = std::make_shared<MExpr>();
  E->K = MExpr::Not;
  E->L = std::move(l);
  return E;
}
// A cast node: mCast("sext"|"zext", operand, targetWidth, srcWidth).
// srcWidth is the source type width in bits. When it is 0 the cast is an
// intermediate that the identities must rewrite away before rendering; when it
// is > 0 it is a *materializable* cast of a narrow opaque variable and is kept
// in the rendered string as op[SW:TW](x), which eval()/createLLVMReplacement()
// turn into a real sext/zext instruction.
MExprPtr mCast(std::string op, MExprPtr l, std::string width, int srcWidth = 0) {
  auto E = std::make_shared<MExpr>();
  E->K = MExpr::Cast;
  E->Op = std::move(op);
  E->L = std::move(l);
  E->S = std::move(width);
  E->SW = srcWidth;
  return E;
}

// Canonical rendering (fully parenthesized, shunting-yard friendly).
std::string mStr(const MExprPtr &E) {
  if (!E) return "?";
  switch (E->K) {
  case MExpr::Var:
    return E->S;
  case MExpr::Const:
    return E->S;
  case MExpr::Not:
    return "~" + mStr(E->L);
  case MExpr::Cast:
    if (E->SW > 0) {
      // Materializable cast of a narrow opaque variable: render with the source
      // width so eval()/createLLVMReplacement() can build a real sext/zext.
      return E->Op + "[" + std::to_string(E->SW) + ":" + E->S + "](" +
             mStr(E->L) + ")";
    }
    // Intermediate cast; if one survives to rendering it is a bug, but render
    // it unambiguously so it is visible in debug output.
    return E->Op + "[" + E->S + "](" + mStr(E->L) + ")";
  case MExpr::Bin:
    return "(" + mStr(E->L) + E->Op + mStr(E->R) + ")";
  }
  return "?";
}

// Number of operators (binary + unary) in the tree.
int mOps(const MExprPtr &E) {
  if (!E) return 0;
  int n = (E->K == MExpr::Bin || E->K == MExpr::Not ||
           E->K == MExpr::Cast)
              ? 1
              : 0;
  if (E->L) n += mOps(E->L);
  if (E->R) n += mOps(E->R);
  return n;
}

// Number of operator tokens in a canonical-form expression string. Each
// operator token becomes one new instruction when the replacement is built, so
// this is the number of instructions the replacement adds. The canonical form
// is fully parenthesized: "(L op R)" for binary, "~L" for unary, and a bare
// token (a single letter or a decimal, optionally negative) for leaves. A '-'
// starts a negative number only when it is followed by a digit and sits where
// a leaf would be (start, or after '(' / an operator); otherwise it is the
// subtraction operator.
int countOpsInString(const std::string &S) {
  auto isOpChar = [](char c) {
    return c == '+' || c == '-' || c == '*' || c == '&' || c == '|' ||
           c == '<' || c == '~';
  };
  int count = 0;
  size_t i = 0;
  while (i < S.size()) {
    char c = S[i];
    bool nextIsDigit =
        i + 1 < S.size() && std::isdigit(static_cast<unsigned char>(S[i + 1]));
    bool leafStart =
        i == 0 || S[i - 1] == '(' || isOpChar(S[i - 1]);
    if (std::isdigit(static_cast<unsigned char>(c))) {
      // Plain (positive) number.
      while (i < S.size() && std::isdigit(static_cast<unsigned char>(S[i])))
        i++;
    } else if (c == '-' && nextIsDigit && leafStart) {
      // Negative number leaf: skip the sign and the digits.
      i += 2;
      while (i < S.size() && std::isdigit(static_cast<unsigned char>(S[i])))
        i++;
    } else if (S.compare(i, 5, "sext[") == 0 ||
               S.compare(i, 5, "zext[") == 0) {
      // Materializable cast: op[SW:TW](operand). One instruction.
      count++;
      auto close = S.find(']', i);
      i = (close == std::string::npos) ? i + 1 : close + 1;
    } else if (isOpChar(c)) {
      // Operator token.
      count++;
      i++;
    } else {
      // Variable letter, parenthesis, or anything else: not an operator.
      i++;
    }
  }
  return count;
}

bool isConstVal(const MExprPtr &E, const std::string &v) {
  return E && E->K == MExpr::Const && E->S == v;
}

bool sameExpr(const MExprPtr &a, const MExprPtr &b) {
  return mStr(a) == mStr(b);
}

// If E is "base * 2" or "base << 1" (either operand order for mul), return
// base; otherwise nullptr.
MExprPtr baseOfTimes2(const MExprPtr &E) {
  if (!E || E->K != MExpr::Bin) return nullptr;
  if (E->Op == "*") {
    if (isConstVal(E->L, "2")) return E->R;
    if (isConstVal(E->R, "2")) return E->L;
  }
  if (E->Op == "<" && isConstVal(E->R, "1")) return E->L;
  return nullptr;
}

// True if E is "x & y" (either operand order).
bool isAndOf(const MExprPtr &E, const MExprPtr &x, const MExprPtr &y) {
  if (!E || E->K != MExpr::Bin || E->Op != "&") return false;
  return (sameExpr(E->L, x) && sameExpr(E->R, y)) ||
         (sameExpr(E->L, y) && sameExpr(E->R, x));
}

// If E is the sign-extension idiom or(shl(zext(ashr(v, N)), M), zext(v)) —
// the classic "sign-extend v to a wider type" obfuscation — return v;
// otherwise nullptr. The two zext operands must target the same width and the
// ashr/shl amounts must be consistent (N = src-1, M = dst-src) so the fold is
// value-correct.
MExprPtr matchSignExt(const MExprPtr &E) {
  if (!E || E->K != MExpr::Bin || E->Op != "|") return nullptr;
  auto A = E->L, B = E->R;
  // A must be shl(zext(ashr(v, N)), M)
  if (!A || A->K != MExpr::Bin || A->Op != "<") return nullptr;
  if (!A->R || A->R->K != MExpr::Const) return nullptr;
  if (!A->L || A->L->K != MExpr::Cast || A->L->Op != "zext") return nullptr;
  auto ashrNode = A->L->L;
  if (!ashrNode || ashrNode->K != MExpr::Bin || ashrNode->Op != "a")
    return nullptr;
  if (!ashrNode->R || ashrNode->R->K != MExpr::Const) return nullptr;
  // B must be zext(v) with the same target width and the same v.
  if (!B || B->K != MExpr::Cast || B->Op != "zext") return nullptr;
  if (A->L->S != B->S) return nullptr;  // same target width
  auto v = ashrNode->L;
  if (!sameExpr(B->L, v)) return nullptr;
  // Consistency: ashr amount N and shl amount M must satisfy M = dst - (N+1).
  long N = std::stol(ashrNode->R->S);
  long M = std::stol(A->R->S);
  long dst = std::stol(B->S);
  long src = N + 1;
  if (dst != src + M) return nullptr;
  return v;
}

// One round of MBA identity rewrites on a (child-simplified) tree.
MExprPtr mIdentities(const MExprPtr &E) {
  if (!E) return E;
  if (E->K == MExpr::Cast) {
    // sext(W, sext(W', v) op c)  ==  sext(W, v) op c   (sign-extension is
    // associative and c is small enough not to overflow the wider type).
    if (E->Op == "sext" && E->L && E->L->K == MExpr::Bin &&
        (E->L->Op == "-" || E->L->Op == "+")) {
      auto X = E->L->L, c = E->L->R;
      if (X && X->K == MExpr::Cast && X->Op == "sext" && c &&
          c->K == MExpr::Const)
        return mBin(E->L->Op, mCast("sext", X->L, E->S), c);
    }
    return E;
  }
  if (E->K != MExpr::Bin) return E;

  // (x + y) - 2*(x & y)  ==  x ^ y
  if (E->Op == "-" && E->L && E->L->K == MExpr::Bin && E->L->Op == "+") {
    auto x = E->L->L, y = E->L->R;
    auto base = baseOfTimes2(E->R);
    if (base && isAndOf(base, x, y))
      return mBin("^", x, y);
  }

  // 2*(x & y) + (x ^ y)  ==  x + y
  if (E->Op == "+") {
    MExprPtr andBase = baseOfTimes2(E->L);
    MExprPtr xorSide = E->R;
    if (!andBase) {
      andBase = baseOfTimes2(E->R);
      xorSide = E->L;
    }
    if (andBase && xorSide && xorSide->K == MExpr::Bin && xorSide->Op == "^") {
      auto x = xorSide->L, y = xorSide->R;
      if (isAndOf(andBase, x, y))
        return mBin("+", x, y);
    }
  }

  // x ^ (x ^ y)  ==  y   and   y ^ (x ^ y)  ==  x
  if (E->Op == "^") {
    auto check = [](MExprPtr outer, MExprPtr inner) -> MExprPtr {
      if (!inner || inner->K != MExpr::Bin || inner->Op != "^") return nullptr;
      auto ix = inner->L, iy = inner->R;
      if (sameExpr(outer, ix)) return iy;
      if (sameExpr(outer, iy)) return ix;
      return nullptr;
    };
    if (auto r = check(E->L, E->R)) return r;
    if (auto r = check(E->R, E->L)) return r;
  }

  // x ^ -1  ==  ~x
  if (E->Op == "^") {
    if (isConstVal(E->L, "-1")) return mNot(E->R);
    if (isConstVal(E->R, "-1")) return mNot(E->L);
  }

  // Sign-extension idiom: or(shl(zext(ashr(v, N)), M), zext(v)) == sext(v).
  // The target width is the zext width recorded on the pattern.
  if (auto v = matchSignExt(E)) {
    long dst = std::stol(E->R->S);  // the zext target width
    return mCast("sext", v, std::to_string(dst));
  }

  return E;
}

// Bottom-up simplification: simplify the children, then apply the identities
// until fixed point (bounded). The loop is needed because one identity can
// produce a node that another identity (on a different Kind) then rewrites —
// e.g. the sign-extension fold produces a Cast node that the distribute
// identity then reduces.
MExprPtr mSimplify(const MExprPtr &E) {
  if (!E) return nullptr;
  if (E->K == MExpr::Var || E->K == MExpr::Const) return E;
  auto L = mSimplify(E->L);
  MExprPtr T;
  if (E->K == MExpr::Not) {
    T = mNot(L);
  } else if (E->K == MExpr::Cast) {
    T = mCast(E->Op, L, E->S);
  } else {
    auto R = mSimplify(E->R);
    T = mBin(E->Op, L, R);
  }
  MExprPtr Result = T;
  for (int i = 0; i < 16; i++) {
    MExprPtr Next = mIdentities(Result);
    if (mStr(Next) == mStr(Result)) {  // fixed point
      Result = Next;
      break;
    }
    Result = Next;
  }
  return Result;
}

// Materialize each surviving cast(TW, v) node (where v is a narrow opaque
// variable of width W' < TW) as a real sext/zext by recording the source width
// (SW = W'). A real cast is value-correct for ALL full-width inputs: verify()
// feeds the opaque variable a full-width random value while the original AST
// implicitly truncates it to its (narrower) type width, but a real sext/zext
// explicitly takes the low W' bits (and sign-extends for sext), so it matches
// the original for every input. Keeping the cast (1 op) is far cheaper than the
// old expansion  (v & (2^W'-1)) - ((v & 2^(W'-1)) << 1)  (4 ops).
MExprPtr mTruncNarrow(const MExprPtr &E,
                      const std::map<std::string, int> &VarWidths) {
  if (!E) return nullptr;
  if (E->K == MExpr::Cast && (E->Op == "sext" || E->Op == "zext") &&
      E->L && E->L->K == MExpr::Var) {
    auto VIt = VarWidths.find(E->L->S);
    if (VIt != VarWidths.end()) {
      int Wp = VIt->second;
      int W = std::stol(E->S);
      if (Wp > 0 && Wp < W) {
        // Materialize: keep the cast, record the source width.
        return mCast(E->Op, E->L, E->S, Wp);
      }
    }
    return E;
  }
  if (E->K == MExpr::Var || E->K == MExpr::Const) return E;
  auto L = mTruncNarrow(E->L, VarWidths);
  if (E->K == MExpr::Bin)
    return mBin(E->Op, L, mTruncNarrow(E->R, VarWidths));
  if (E->K == MExpr::Not)
    return mNot(L);
  if (E->K == MExpr::Cast)
    return mCast(E->Op, L, E->S, E->SW);
  return E;
}
}  // namespace

bool LLVMParser::tryMBAPatterns(LSiMBA::MBACandidate &Cand, int BitWidth) {
  // Map the candidate's opaque variables to single-letter names in the same
  // order the verifier uses (Variables[0] -> 'a', [1] -> 'b', ...).
  llvm::DenseMap<llvm::Value *, std::string> VarNames;
  // Per-opaque-variable type width (in bits), keyed by the same single-letter
  // name, so mTruncNarrow can rewrite a narrow variable's sext into the
  // value-correct truncation form.
  std::map<std::string, int> VarWidths;
  char c = 'a';
  for (auto *V : Cand.Variables) {
    std::string Name(1, c++);
    VarNames[V] = Name;
    if (auto *Ty = llvm::dyn_cast<llvm::IntegerType>(V->getType()))
      VarWidths[Name] = Ty->getBitWidth();
  }

  // Build a canonical tree for every instruction in the AST. The AST is
  // ordered so that operands come after their uses (evaluateAST walks it
  // rbegin -> rend), so iterate deepest-first: each instruction's operand
  // instructions are already built by the time we reach them.
  llvm::DenseMap<llvm::Value *, MExprPtr> Orig;
  llvm::DenseMap<llvm::Value *, MExprPtr> Simp;

  auto makeConst = [](llvm::Value *V) -> MExprPtr {
    auto *CI = llvm::dyn_cast<llvm::ConstantInt>(V);
    if (!CI) return nullptr;
    const llvm::APInt &A = CI->getValue();
    std::string S;
    if (A.isNegative()) {
      // Two's-complement magnitude, rendered as a signed decimal.
      auto Mag = (~A) + 1;
      S = "-" + std::to_string(Mag.getZExtValue());
    } else {
      S = std::to_string(A.getZExtValue());
    }
    return mConst(S);
  };

  auto resolveOrig = [&](llvm::Value *V) -> MExprPtr {
    if (auto T = makeConst(V)) return T;
    auto VN = VarNames.find(V);
    if (VN != VarNames.end()) return mVar(VN->second);
    if (auto *I = llvm::dyn_cast<llvm::Instruction>(V)) {
      auto It = Orig.find(I);
      if (It != Orig.end()) return It->second;
    }
    return nullptr;
  };

  auto resolveSimp = [&](llvm::Value *V) -> MExprPtr {
    if (auto T = makeConst(V)) return T;
    auto VN = VarNames.find(V);
    if (VN != VarNames.end()) return mVar(VN->second);
    if (auto *I = llvm::dyn_cast<llvm::Instruction>(V)) {
      auto It = Simp.find(I);
      if (It != Simp.end()) return It->second;
    }
    return nullptr;
  };

  // Build the tree for an instruction from a resolve function. The binary
  // operators consistent between eval() and createLLVMReplacement() are used
  // (+ - ^ & | * < and ashr, which is only needed to recognize the
  // sign-extension pattern below). zext/sext are represented as Cast nodes;
  // the identities rewrite them away before rendering. Anything else (lshr /
  // sdiv / udiv / urem / non-binary / non-cast) bails out.
  auto buildOne = [&](llvm::Instruction *I, auto &&resolve) -> MExprPtr {
    if (auto *ZExt = llvm::dyn_cast<llvm::ZExtInst>(I)) {
      MExprPtr L = resolve(ZExt->getOperand(0));
      if (!L) return nullptr;
      return mCast("zext", L, std::to_string(
                   ZExt->getType()->getIntegerBitWidth()));
    }
    if (auto *SExt = llvm::dyn_cast<llvm::SExtInst>(I)) {
      MExprPtr L = resolve(SExt->getOperand(0));
      if (!L) return nullptr;
      return mCast("sext", L, std::to_string(
                   SExt->getType()->getIntegerBitWidth()));
    }
    auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(I);
    if (!BO) return nullptr;
    MExprPtr L = resolve(BO->getOperand(0));
    MExprPtr R = resolve(BO->getOperand(1));
    if (!L || !R) return nullptr;
    std::string op;
    switch (BO->getOpcode()) {
    case llvm::Instruction::Add:
      op = "+";
      break;
    case llvm::Instruction::Sub:
      op = "-";
      break;
    case llvm::Instruction::Xor:
      op = "^";
      break;
    case llvm::Instruction::And:
      op = "&";
      break;
    case llvm::Instruction::Or:
      op = "|";
      break;
    case llvm::Instruction::Mul:
      op = "*";
      break;
    case llvm::Instruction::Shl:
      op = "<";
      break;
    case llvm::Instruction::AShr:
      op = "a";
      break;
    default:
      return nullptr;
    }
    return mBin(op, L, R);
  };

  for (auto It = Cand.AST.rbegin(); It != Cand.AST.rend(); ++It) {
    auto *I = It->I;
    if (Orig.count(I)) continue;
    MExprPtr T = buildOne(I, resolveOrig);
    if (!T) return false;  // an operand we cannot express; bail out
    Orig[I] = T;
    MExprPtr TS = buildOne(I, resolveSimp);
    if (!TS) return false;
    Simp[I] = mSimplify(TS);
  }

  if (Orig.count(Cand.AST.front().I) == 0) return false;
  MExprPtr OrigRoot = Orig[Cand.AST.front().I];
  MExprPtr SimpRoot = Simp[Cand.AST.front().I];
  // Rewrite any narrow opaque variable's surviving sext/zext into a real,
  // materializable cast (see mTruncNarrow) so the replacement matches the
  // original AST for all full-width inputs. The cast handler in
  // createLLVMReplacement/eval recovers the original narrow value and takes
  // the low source-width bits, so the rendered sext[SW:TW] is value-correct.
  SimpRoot = mTruncNarrow(SimpRoot, VarWidths);

  int OrigOps = mOps(OrigRoot);
  int SimpOps = mOps(SimpRoot);
  if (SimpOps >= OrigOps) return false;  // no structural improvement

  Cand.Replacement = mStr(SimpRoot);

  // Net-reduction gate. Applying the replacement only redirects the root's uses
  // to a freshly built expression; it does not by itself remove any old
  // instruction. An old AST instruction is only removable if it ends up with no
  // remaining uses, but intermediate values shared with code outside the AST
  // stay live. So a rewrite that is structurally smaller can still *increase*
  // the instruction count when its intermediates are shared. Apply it only if
  // it is a strict net reduction in instruction count, otherwise it would just
  // add redundant work.
  {
    llvm::DenseSet<llvm::Instruction *> ASTSet;
    for (auto &E : Cand.AST) ASTSet.insert(E.I);
    auto *Root = Cand.AST.front().I;

    // An AST instruction is live if it has a use outside the AST, or is used
    // by a live AST instruction. The root is always dead: its uses are
    // redirected to the new expression, and the new expression is built from
    // the opaque variables (outside the AST), not from any old AST
    // instruction.
    llvm::DenseSet<llvm::Instruction *> Live;
    for (auto *I : ASTSet) {
      if (I == Root) continue;
      for (auto &U : I->uses()) {
        auto *User = llvm::dyn_cast<llvm::Instruction>(U.getUser());
        if (!User || !ASTSet.count(User)) {
          Live.insert(I);
          break;
        }
      }
    }
    bool Changed = true;
    while (Changed) {
      Changed = false;
      for (auto *I : ASTSet) {
        if (I == Root || Live.count(I)) continue;
        for (auto &U : I->uses()) {
          auto *User = llvm::dyn_cast<llvm::Instruction>(U.getUser());
          if (User && ASTSet.count(User) && Live.count(User)) {
            Live.insert(I);
            Changed = true;
            break;
          }
        }
      }
    }

    int DeadCount = 0;
    for (auto *I : ASTSet)
      if (!Live.count(I)) DeadCount++;

    int NewCount = countOpsInString(Cand.Replacement);

    if (NewCount >= DeadCount) {
      if (this->Debug) {
        outs() << "[*] [MBAPATTERN] Skipping (not a net reduction): dead="
               << DeadCount << " new=" << NewCount << "\n";
      }
      return false;
    }

    if (this->Debug) {
      outs() << "[*] [MBAPATTERN] ASTSize=" << Cand.ASTSize << " orig '"
             << mStr(OrigRoot) << "' (" << OrigOps << " ops) -> '"
             << Cand.Replacement << "' (" << SimpOps << " ops); net: erase="
             << DeadCount << " add=" << NewCount << "\n";
    }
  }

  return true;
}

bool LLVMParser::findReplacements(llvm::DominatorTree *DT,
                                  std::vector<MBACandidate> &Candidates) {
  if (Candidates.empty()) {
    return false;
  }

  bool ReplacementFound = false;

#ifdef DEBUG_SIMPLIFICATION
  // Debug out
  // Candidates.front().Candidate->getFunction()->print(outs());
#endif

  // Search for replacements
  std::vector<MBACandidate> SubASTCandidates;
  auto StartTime = high_resolution_clock::now();
  for (int i = 0; i < Candidates.size(); i++) {
    auto &Cand = Candidates[i];
    getAST(DT, Cand.Candidate, Cand.AST, Cand.Variables, true);
    Cand.ASTSize = getASTSize(Cand.AST);
  }

  auto EndTime = high_resolution_clock::now();
  auto Duration = duration_cast<milliseconds>(EndTime - StartTime);
#ifdef DEBUG_SIMPLIFICATION
  outs() << "[*] Extracted ASTs in " << Duration.count() << " ms\n";

#endif

  // Sort Candidates by AST size
  std::sort(Candidates.begin(), Candidates.end(),
            [](const MBACandidate &A, const MBACandidate &B) {
              return A.AST.size() > B.AST.size();
            });

  // To not solve things twice we keep track of replaced instructions
  llvm::SmallPtrSet<llvm::Instruction *, 16> ReplacedInstructions;

  for (int i = 0; i < Candidates.size(); i++) {
    auto &Cand = Candidates[i];
    if (Cand.ASTSize < MinASTSize) {
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

    // Only handle max xx Vars
    if (Cand.Variables.size() > MaxVarCount) {
#ifdef DEBUG_SIMPLIFICATION
      outs() << "[*] Skipping too many variables: " << Cand.Variables.size()
             << "\n";
#endif
      Cand.isValid = false;
      continue;
    }

    // Dont work on vector types
    if (Cand.AST.front().I->getType()->isVectorTy()) {
      Cand.isValid = false;
      continue;
    }

    // Skip Ptr types
    // We support it now but better test it further!
    /*
    if (Cand.AST.front().I->getType()->isPointerTy()) {
      Cand.isValid = false;
      continue;
    }
    */

    // Check if we already replaced this instruction
    if (constainsReplacedInstructions(ReplacedInstructions, Cand)) {
#ifdef DEBUG_SIMPLIFICATION
      outs() << "[*] Skipping already replaced instruction\n";
#endif
      Cand.isValid = false;
      continue;
    }

    // Try to simplify the whole AST
    int BitWidth = 0;
    if (Cand.AST.front().I->getType()->isPointerTy()) {
      BitWidth = 64;
    } else {
      BitWidth = Cand.AST.front().I->getType()->getIntegerBitWidth();
    }

    if (BitWidth == 0 || BitWidth > 64) {
      // If BitWidth is zero then stop here
      continue;
    }

    // Check if in cache
    bool AlreadyProved = false;
    auto Hash = calculateHash(Cand.AST);
    auto Entry = MBACache.find(Hash);
    if (Entry != MBACache.end()) {
      Cand.isValid = Entry->second;
      AlreadyProved = true;

      // Skip non valid candidates
      if (Cand.isValid == false) continue;
    }

    auto Modulus = getModulus(BitWidth);

    std::vector<APInt> ResultVector;
    initResultVectorFromAST(Cand.AST, ResultVector, Modulus, Cand.Variables,
                            BitWidth);

    // Simplify MBA
    Simplifier S(BitWidth, false, Cand.Variables.size(), ResultVector);
    bool SkipVerify = false;

    // Useful for debugging
#ifdef DEBUG_SIMPLIFICATION
    auto Expr = getASTAsString(Cand.AST, Cand.Variables);
    outs() << "[*] Simplifying Expression: " << Expr << "\n";

    /*
    auto F = getASTasLLVMFunction(this->M, Cand.AST, Cand.Variables);
    F->dump();
    F->eraseFromParent();
    */
#endif
    // Phase 9: route to the selected simplifier (--simplifier). The native
    // selection (default) keeps the original path below untouched.
    bool Routed = false;

    // Exclude candidates containing an arithmetic shift (AShr) from the routed
    // path: an arithmetic shift is not a logical '>>' when the top bit is set,
    // so routing it to the GAMBA ring would be semantically wrong. Such
    // candidates fall back to the native path below.
    bool ArithShift = ASTContainsArithmeticShift(Cand.AST);
    if (ArithShift && this->Debug) {
      outs() << "[*] Candidate contains an arithmetic shift; skipping the "
                "routed path\n";
    }

    // Also exclude candidates containing an instruction getASTAsString cannot
    // render (select / icmp / intrinsic calls such as llvm.bswap). These cannot
    // be expressed as a GAMBA-ring string, so they fall back to the native path.
    bool Unrenderable = ASTHasUnrenderableInstruction(Cand.AST);
    if (Unrenderable && this->Debug) {
      outs() << "[*] Candidate contains an instruction the GAMBA-ring string "
                "renderer cannot handle; skipping the routed path\n";
    }

    if (!ArithShift && !Unrenderable) {
      auto Expr = getASTAsString(Cand.AST, Cand.Variables);

      if (this->Debug) {
        outs() << "[*] Routing expression (BitWidth: " << BitWidth
               << "): '" << Expr << "'\n";
      }

      Routed = LSiMBA::TrySelectedSimplifier(Expr, Cand.Replacement, BitWidth,
                                             this->Prove,
                                             LSiMBA::autoFallbackEnabled());
      if (Routed && this->Debug) {
        outs() << "[*] Selected simplifier produced: '" << Cand.Replacement
               << "'\n";
      }
    }

    // Use external simplifier (also skipped for arithmetic-shift / unrenderable
    // candidates).
    if (!Routed && !ArithShift && !Unrenderable &&
        !UseExternalSimplifier.empty()) {
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
    } else if (!Routed) {
      S.simplify(Cand.Replacement, false, false);
      if (this->Debug) {
        outs() << "[*] [NATIVE] '" << Cand.Replacement << "'\n";
      }
      // Auto-fallback: native produced nothing (e.g. checkLinear classified
      // the expression as linear but the native simplifier cannot reduce it,
      // while msimba/general could). Try the other routes before giving up.
      if (Cand.Replacement.empty() && LSiMBA::autoFallbackActive()) {
        auto Expr = getASTAsString(Cand.AST, Cand.Variables);
        std::string fb;
        if (LSiMBA::TryAutoFallback(Expr, fb, BitWidth, this->Prove, false,
                                    "")) {
          Cand.Replacement = fb;
          if (this->Debug) {
            outs() << "[*] [AUTO-FALLBACK] '" << Cand.Replacement << "'\n";
          }
        }
      }
    }

    // Verify is replacement is valid
    if (!AlreadyProved && !SkipVerify) {
      Cand.isValid = this->verify(Cand.ASTSize, Cand.AST, Cand.Replacement,
                                  Cand.Variables, BitWidth);
    }

    // Match some patterns
    if (!AlreadyProved && !Cand.isValid) {
      bool IsRepl = replaceWithKnownPatterns(Cand, ResultVector);
      if (IsRepl) {
        Cand.isValid = this->verify(Cand.ASTSize, Cand.AST, Cand.Replacement,
                                    Cand.Variables, BitWidth);
      }
      // Local MBA pattern matching: recognize the classic mixed
      // boolean-arithmetic identities in the instruction sequence (operates on
      // intermediate values, so it catches the obfuscated 64-bit MBAs the
      // {0,1} truth table cannot). Works for any bit width.
      if (!Cand.isValid) {
        if (tryMBAPatterns(Cand, BitWidth)) {
          // The local identities are algebraic laws (always true), so the
          // random-value quick test is sufficient; skip the (slow, timeout-prone)
          // Z3 proof for these.
          Cand.isValid = this->verify(Cand.ASTSize, Cand.AST, Cand.Replacement,
                                      Cand.Variables, BitWidth, /*DoZ3=*/false);
        }
      }
      // Wide-type fallback: the {0,1} truth table is insufficient for >=32-bit
      // candidates (the native fit goes spurious). Propose simple rewrites and
      // keep the first one verify()/Z3 proves equivalent.
      if (!Cand.isValid && BitWidth >= 32) {
        if (tryCandidateSimplifications(Cand, BitWidth)) {
          Cand.isValid = true; // already verified inside
        }
      }
    }

    // Update cache
    if (!AlreadyProved) MBACache[Hash] = Cand.isValid;

    if (Cand.isValid == false) {
      // Could not simplify the whole AST so walk through SubASTs
      if (!AlreadyProved && ShouldWalkSubAST)
        ReplacementFound |= walkSubAST(DT, Cand.AST, SubASTCandidates);
    } else {
      if (this->Debug) {
        outs() << "[*] Full AST Simplified Expression: " << Cand.Replacement
               << "\n";
      }

      // Fill vector with replaced instructions to not solve them again
      for (auto &E : Cand.AST) {
        // if (E.I->getType()->isIntegerTy()) {
        ReplacedInstructions.insert(E.I);
        //}
      }

      ReplacementFound |= true;
    }
  }

  // Clean up Candidates and keep only valid ones
  std::vector<MBACandidate> ValidCandidates;

  for (auto &C : Candidates) {
    if (!C.isValid) continue;

    ValidCandidates.push_back(C);
  }

  // Merge candidates with new candidates
  for (auto &C : SubASTCandidates) {
    if (!C.isValid) continue;

    ValidCandidates.push_back(C);
  }

  Candidates = ValidCandidates;

  return ReplacementFound;
}

int LLVMParser::getASTSize(llvm::SmallVectorImpl<BFSEntry> &AST) {
  int Size = 0;
  for (auto e : AST) {
    // Dont count cast/sext/zext
    if (e.I->isCast()) continue;

    Size++;
  }
  return Size;
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
      if (!BinOp) continue;

      // Only work on supported operands
      if (ConstantExpr::isSupportedBinOp(BinOp->getOpcode()) == false) continue;

      MBACandidate C;
      C.Candidate = BinOp;

      this->getAST(DT, BinOp, C.AST, C.Variables, true);
      C.ASTSize = getASTSize(C.AST);

      if (C.ASTSize < MinASTSize) continue;

      int BitWidth = C.AST.front().I->getType()->getIntegerBitWidth();
      if (BitWidth == 0 || BitWidth > 64) continue;

      auto Modulus = getModulus(BitWidth);

      std::vector<APInt> ResultVector;
      initResultVectorFromAST(C.AST, ResultVector, Modulus, C.Variables,
                              BitWidth);

      // Simplify MBA
      bool SkipVerify = false;
      Simplifier S(BitWidth, false, C.Variables.size(), ResultVector);

      // Phase 9: route to the selected simplifier (--simplifier). The native
      // selection (default) keeps the original path below untouched.
      bool Routed = false;
      bool Unrenderable = ASTHasUnrenderableInstruction(C.AST);
      if (!Unrenderable) {
        auto Expr = getASTAsString(C.AST, C.Variables);
        Routed = LSiMBA::TrySelectedSimplifier(Expr, C.Replacement, BitWidth,
                                               this->Prove,
                                               LSiMBA::autoFallbackEnabled());
      }

      if (!Routed && !Unrenderable && !UseExternalSimplifier.empty()) {
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
      } else if (!Routed) {
        S.simplify(C.Replacement, false, false);
      }

#ifdef DEBUG_SIMPLIFICATION
      if (!SkipVerify) {
        printAST(C.AST);
        outs() << "[*] Simplified Expression: " << C.Replacement << "\n";
      }
#endif

      if (!SkipVerify) {
        C.isValid = this->verify(C.ASTSize, C.AST, C.Replacement, C.Variables,
                                 BitWidth);
      }

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
    llvm::SmallVectorImpl<llvm::Value *> &Variables, int BitWidth) {
  // Evalute AST
  int VNumber = Variables.size();
  // auto BitWidth = AST.front().I->getType()->getIntegerBitWidth();

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
  outs() << "[*] AST (Operators: " << getASTSize(AST) << "):\n";

  for (auto E = AST.rbegin(); E != AST.rend(); ++E) {
    auto &e = *E;
    outs() << e.Depth << ": ";
    e.I->print(outs());
    outs() << "\n";
  }
}

uint64_t LLVMParser::calculateHash(llvm::SmallVectorImpl<BFSEntry> &AST) {
  uint64_t x = 0x2545F4914F6CDD1DULL;
  for (auto E = AST.rbegin(); E != AST.rend(); ++E) {
    auto &e = *E;
    x += e.I->getOpcode();

    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;

    // Hash the operands  type
    // Has impact on performance!
    for (auto &Op : e.I->operands()) {
      x += Op->getType()->getTypeID();

      // Check if its a constant
      if (auto C = dyn_cast<ConstantInt>(Op)) {
        x += C->getValue().getLimitedValue();
      }

      x ^= x << 13;
      x ^= x >> 7;
      x ^= x << 17;
    }
  }
  return x;
}

// True iff the AST contains an arithmetic shift (AShr). AShr is not expressible
// as a logical '>>' in the GAMBA ring: it differs from LShr whenever the top bit
// of the value is set. Such candidates are therefore excluded from the routed
// (general/external) path and fall back to the native path instead.
static bool ASTContainsArithmeticShift(llvm::SmallVectorImpl<BFSEntry> &AST) {
  for (auto &E : AST) {
    if (auto B = llvm::dyn_cast<llvm::BinaryOperator>(E.I))
      if (B->getOpcode() == llvm::Instruction::AShr)
        return true;
  }
  return false;
}

// Per-instruction version: true iff a single instruction is one that
// getASTAsString cannot render as a GAMBA-ring string. getASTAsString only
// understands BinaryOperator, GEP, Trunc, ZExt and SExt; anything else (a
// select, an icmp, or an intrinsic call such as llvm.bswap / llvm.ctpop /
// llvm.bitreverse) is "unrenderable". Used by getAST to "cut" at such
// instructions: instead of recursing into them (which would taint the whole
// candidate as unrenderable), they are treated as opaque leaves / variables so
// the surrounding MBA sub-expression can still be rendered and simplified.
static bool isUnrenderableInstruction(llvm::Value *V) {
  if (llvm::isa<llvm::BinaryOperator>(V)) return false;
  if (llvm::isa<llvm::GetElementPtrInst>(V)) return false;
  if (llvm::isa<llvm::TruncInst>(V)) return false;
  if (llvm::isa<llvm::ZExtInst>(V)) return false;
  if (llvm::isa<llvm::SExtInst>(V)) return false;
  return true;
}

// True iff the AST contains an instruction that getASTAsString cannot render as
// a GAMBA-ring string. Such candidates are excluded from the routed path and
// fall back to the native path instead.
static bool ASTHasUnrenderableInstruction(llvm::SmallVectorImpl<BFSEntry> &AST) {
  for (auto &E : AST) {
    if (isUnrenderableInstruction(E.I)) return true;
  }
  return false;
}

std::string LLVMParser::getASTAsString(
    llvm::SmallVectorImpl<BFSEntry> &AST,
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
          // Unsigned semantics only: MBA values are interpreted mod 2^B, so this
          // is rendered as an unsigned '/'. Signed division rounds toward zero and
          // is not the same for negative (top-bit-set) values; the fast-check gate
          // catches any mismatch on the routed path.
          Expr += " / ";
          break;
        case Instruction::URem:
          Expr += " % ";
          break;
        case Instruction::SRem:
          // Unsigned semantics only (see SDiv): rendered as an unsigned '%'.
          Expr += " % ";
          break;
        case Instruction::Shl:
          Expr += " << ";
          break;
        case Instruction::LShr:
          Expr += " >> ";
          break;
        case Instruction::AShr:
          // Emitted as '>>' for display/debugging only. Candidates containing an
          // AShr are excluded from the routed path (see ASTContainsArithmeticShift)
          // because an arithmetic shift is not a logical '>>' when the top bit is
          // set; they fall back to the native path.
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
    } else if (auto GEP = dyn_cast<GetElementPtrInst>(CurInst)) {
      if (auto C = dyn_cast<ConstantInt>(GEP->getOperand(0))) {
        SmallString<16> StrC;
        C->getValue().toString(StrC, 10, true);
        Expr += StrC;
      } else {
        Expr += VariableMap[GEP->getOperand(0)];
      }

      Expr += " + ";

      if (auto C = dyn_cast<ConstantInt>(GEP->getOperand(1))) {
        SmallString<16> StrC;
        C->getValue().toString(StrC, 10, true);
        Expr += StrC;
      } else {
        Expr += VariableMap[GEP->getOperand(1)];
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
      // 1bit: (x & 0x1) - (x & 0x2)
      switch (SExt->getSrcTy()->getIntegerBitWidth()) {
        case 1:
          Expr += " & 0x1) - (";
          Expr += VariableMap[Op0];
          Expr += " & 0x2)";
          break;
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
          outs() << "BitWidth: " << SExt->getSrcTy()->getIntegerBitWidth()
                 << "\n";
          report_fatal_error("Unsupported bit width!");
      }
    } else {
      outs() << "[getASTAsString] Unsupported instruction! : '";
      CurInst->print(outs());
      outs() << "'\n";

      return "";
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
    if (!Ins) continue;

    for (auto &O : Ins->operands()) {
      if (isa<Constant>(O)) continue;

      // Must be a variable
      if (isa<Argument>(O)) {
        Vars.insert(O);
        continue;
      }

      if (auto OpIns = dyn_cast<Instruction>(O)) {
        if (Dis.find(OpIns) == Dis.end()) {
          // Check if supported. Also "cut" at instructions getASTAsString cannot
          // render (intrinsic calls such as llvm.bswap): treat them as opaque
          // leaves / variables instead of recursing into them, so the
          // surrounding MBA sub-expression stays renderable and can be
          // simplified (the standard "cut at opaque calls" MBA technique).
          //
          // Select and icmp are the exception. Although getASTAsString cannot
          // render them either, they ARE evaluatable (see evaluateAST and the
          // Z3 encoder getZ3Val), so we inline them into the AST instead of
          // cutting them as independent leaves. Cutting a select that actually
          // depends on another variable (e.g. the wraparound-correction select
          // in llvm/lifted.ll, which is the carry-out bit of v + 47282) made
          // it look like a free variable; the {0,1} truth-table fit then went
          // spurious and verify() rejected every candidate. Inlining keeps the
          // expression a true function of the remaining leaves, so the native
          // fit and verify() now agree. (The candidate is still skipped by the
          // routed/external path via ASTHasUnrenderableInstruction, which is
          // correct: a select/icmp cannot be expressed as a GAMBA-ring string.)
          bool Cut = !isSupportedInstruction(OpIns);
          if (!Cut && isUnrenderableInstruction(OpIns) &&
              !llvm::isa<llvm::SelectInst>(OpIns) &&
              !llvm::isa<llvm::ICmpInst>(OpIns)) {
            Cut = true;
          }
          if (Cut) {
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

llvm::APInt LLVMParser::evaluateAST(
    llvm::SmallVectorImpl<BFSEntry> &AST,
    llvm::SmallVectorImpl<llvm::Value *> &Variables,
    llvm::SmallVectorImpl<APInt> &Par, bool &Error) {
  Constant *InstResult = nullptr;

  // has a big performance impact
  llvm::SmallDenseMap<llvm::Value *, llvm::Constant *, 16> ValueStack;

  for (auto E = AST.rbegin(); E != AST.rend(); ++E) {
    auto CurInst = E->I;

    if (auto BO = dyn_cast<BinaryOperator>(CurInst)) {
      ConstantInt *Op0 = dyn_cast<ConstantInt>(
          getVal(BO->getOperand(0), ValueStack, Variables, Par));
      ConstantInt *Op1 = dyn_cast<ConstantInt>(
          getVal(BO->getOperand(1), ValueStack, Variables, Par));

      switch (BO->getOpcode()) {
        case Instruction::Shl:
          InstResult = ConstantInt::get(BO->getType(),
                                        Op0->getValue().shl(Op1->getValue()));
          break;
        case Instruction::LShr:
          InstResult = ConstantInt::get(BO->getType(),
                                        Op0->getValue().lshr(Op1->getValue()));
          break;
        case Instruction::AShr:
          InstResult = ConstantInt::get(BO->getType(),
                                        Op0->getValue().ashr(Op1->getValue()));
          break;
        case Instruction::And:
          InstResult = ConstantInt::get(BO->getType(),
                                        Op0->getValue() & Op1->getValue());
          break;
        case Instruction::Or:
          InstResult = ConstantInt::get(BO->getType(),
                                        Op0->getValue() | Op1->getValue());
          break;
        case Instruction::UDiv:
          // Unsigned divide; guard div-by-zero (ConstantExpr::get would fold
          // a /0 to undefined and can crash).
          InstResult = ConstantInt::get(
              BO->getType(), Op1->getValue().isZero()
                                 ? APInt(BO->getType()->getIntegerBitWidth(), 0)
                                 : Op0->getValue().udiv(Op1->getValue()));
          break;
        case Instruction::URem:
          // Unsigned remainder; guard div-by-zero.
          InstResult = ConstantInt::get(
              BO->getType(), Op1->getValue().isZero()
                                 ? APInt(BO->getType()->getIntegerBitWidth(), 0)
                                 : Op0->getValue().urem(Op1->getValue()));
          break;
        default: {
          InstResult = ConstantExpr::get(
              BO->getOpcode(), getVal(Op0, ValueStack, Variables, Par),
              getVal(Op1, ValueStack, Variables, Par));
        };
      }
    } else if (auto GEP = dyn_cast<GetElementPtrInst>(CurInst)) {
      // Must be a PtrAdd
      ConstantInt *Ptr = dyn_cast<ConstantInt>(
          getVal(GEP->getOperand(0), ValueStack, Variables, Par));
      ConstantInt *Index = dyn_cast<ConstantInt>(
          getVal(GEP->getOperand(1), ValueStack, Variables, Par));

      if (!Ptr || !Index) {
        // One of the operands evaluated to a Constant that isn't a plain
        // integer (e.g. a genuine pointer-typed constant such as a null
        // or inttoptr base, rather than the "pointers modeled as
        // integers" shape this GEP case otherwise assumes) - not
        // evaluable here. Fail gracefully through the same Error
        // contract the caller already relies on for every other
        // unevaluable expression (see the InstResult==null check at the
        // end of this function), instead of dereferencing a null
        // ConstantInt*.
        Error = true;
        return APInt(1, 0);
      }

      // Todo: Ensure its 64bit type here
      InstResult = ConstantInt::get(Index->getType(),
                                    Ptr->getValue() + Index->getValue());
    } else if (auto Trunc = dyn_cast<TruncInst>(CurInst)) {
      // %27 = trunc i64 %26 to i32
      InstResult = ConstantExpr::getTrunc(
          getVal(Trunc->getOperand(0), ValueStack, Variables, Par),
          Trunc->getType());
    } else if (auto ZExt = dyn_cast<ZExtInst>(CurInst)) {
      InstResult = ConstantFoldCastInstruction(
          Instruction::ZExt,
          getVal(ZExt->getOperand(0), ValueStack, Variables, Par),
          ZExt->getType());
    } else if (auto SExt = dyn_cast<SExtInst>(CurInst)) {
      InstResult = ConstantFoldCastInstruction(
          Instruction::SExt,
          getVal(SExt->getOperand(0), ValueStack, Variables, Par),
          SExt->getType());
    } else if (auto SI = dyn_cast<SelectInst>(CurInst)) {
      auto a = getVal(SI->getOperand(0), ValueStack, Variables, Par);
      auto b = getVal(SI->getOperand(1), ValueStack, Variables, Par);
      auto c = getVal(SI->getOperand(2), ValueStack, Variables, Par);

      InstResult = ConstantFoldSelectInstruction(a, b, c);
    } else if (auto CI = dyn_cast<ICmpInst>(CurInst)) {
      InstResult = ConstantFoldCompareInstruction(
          CI->getPredicate(),
          getVal(CI->getOperand(0), ValueStack, Variables, Par),
          getVal(CI->getOperand(1), ValueStack, Variables, Par));
    } else if (auto Call = dyn_cast<CallInst>(CurInst)) {
      auto CI = Call->getCalledFunction();
      switch (CI->getIntrinsicID()) {
        case Intrinsic::fshl: {
          // Implement as rotate left algorithm
          auto Op0 = getVal(Call->getArgOperand(0), ValueStack, Variables, Par);
          auto Op1 = getVal(Call->getArgOperand(1), ValueStack, Variables, Par);
          auto Op2 = getVal(Call->getArgOperand(2), ValueStack, Variables, Par);

          // Get constant value
          auto a = dyn_cast<ConstantInt>(Op0)->getZExtValue();
          auto b = dyn_cast<ConstantInt>(Op1)->getZExtValue();
          auto c = dyn_cast<ConstantInt>(Op2)->getZExtValue();

          auto width = Op0->getType()->getIntegerBitWidth();
          auto c_mod_width = c % width;

          // Rotate left
          auto r = a << c_mod_width | (b >> (width - c_mod_width));

          // Set result
          InstResult = ConstantInt::get(Op0->getType(), r);
        } break;
        case Intrinsic::fshr: {
          // Implement as rotate right algorithm
          auto Op0 = getVal(Call->getArgOperand(0), ValueStack, Variables, Par);
          auto Op1 = getVal(Call->getArgOperand(1), ValueStack, Variables, Par);
          auto Op2 = getVal(Call->getArgOperand(2), ValueStack, Variables, Par);

          // Get constant value
          auto a = dyn_cast<ConstantInt>(Op0)->getZExtValue();
          auto b = dyn_cast<ConstantInt>(Op1)->getZExtValue();
          auto c = dyn_cast<ConstantInt>(Op2)->getZExtValue();

          auto width = Op0->getType()->getIntegerBitWidth();
          auto c_mod_width = c % width;

          // Rotate right
          auto r = a << (width - c_mod_width) | (b >> c_mod_width);

          // Set result
          InstResult = ConstantInt::get(Op0->getType(), r);
        } break;
        case Intrinsic::bitreverse: {
          auto Op0 = getVal(Call->getArgOperand(0), ValueStack, Variables, Par);
          auto a = dyn_cast<ConstantInt>(Op0)->getZExtValue();

          switch (Op0->getType()->getIntegerBitWidth()) {
            case 16: {
              auto r = reverseBits<uint16_t>(a);
              InstResult = ConstantInt::get(Op0->getType(), r);
            } break;
            case 32: {
              auto r = reverseBits<uint32_t>(a);
              InstResult = ConstantInt::get(Op0->getType(), r);
            } break;
            case 64: {
              auto r = reverseBits<uint64_t>(a);
              InstResult = ConstantInt::get(Op0->getType(), r);
            } break;
            default: {
              CI->dump();
              outs() << Op0->getType()->getIntegerBitWidth() << "\n";
              report_fatal_error("[!] Not supported bitreverse!", false);
            }
          }
        } break;
        case Intrinsic::ctpop: {
          auto Op0 = getVal(Call->getArgOperand(0), ValueStack, Variables, Par);
          auto a = dyn_cast<ConstantInt>(Op0)->getZExtValue();
          auto r = __builtin_popcount(a);
          InstResult = ConstantInt::get(Op0->getType(), r);
        } break;
        case Intrinsic::bswap: {
          auto Op0 = getVal(Call->getArgOperand(0), ValueStack, Variables, Par);
          auto a = dyn_cast<ConstantInt>(Op0)->getZExtValue();
          switch (Op0->getType()->getIntegerBitWidth()) {
            case 16: {
              auto r = __builtin_bswap16(a);
              InstResult = ConstantInt::get(Op0->getType(), r);
            } break;
            case 32: {
              auto r = __builtin_bswap32(a);
              InstResult = ConstantInt::get(Op0->getType(), r);
            } break;
            case 64: {
              auto r = __builtin_bswap64(a);
              InstResult = ConstantInt::get(Op0->getType(), r);
            } break;
            default: {
              CI->dump();
              outs() << Op0->getType()->getIntegerBitWidth() << "\n";
              report_fatal_error("[!] Not supported bswap!", false);
            }
          }
          break;
        }
        case Intrinsic::umax: {
          auto Op0 = getVal(Call->getArgOperand(0), ValueStack, Variables, Par);
          auto Op1 = getVal(Call->getArgOperand(1), ValueStack, Variables, Par);
          if (Op0 <= Op1) {
            InstResult = Op1;
          } else {
            InstResult = Op0;
          }
        } break;
        case Intrinsic::umin: {
          auto Op0 = getVal(Call->getArgOperand(0), ValueStack, Variables, Par);
          auto Op1 = getVal(Call->getArgOperand(1), ValueStack, Variables, Par);
          if (Op0 <= Op1) {
            InstResult = Op0;
          } else {
            InstResult = Op1;
          }
        } break;
        case Intrinsic::smin: {
          auto Op0 = getVal(Call->getArgOperand(0), ValueStack, Variables, Par);
          auto Op1 = getVal(Call->getArgOperand(1), ValueStack, Variables, Par);
          if (Op0 <= Op1) {
            InstResult = Op0;
          } else {
            InstResult = Op1;
          }
        } break;
        case Intrinsic::smax: {
          auto Op0 = getVal(Call->getArgOperand(0), ValueStack, Variables, Par);
          auto Op1 = getVal(Call->getArgOperand(1), ValueStack, Variables, Par);
          if (Op0 <= Op1) {
            InstResult = Op1;
          } else {
            InstResult = Op0;
          }
        } break;
        case Intrinsic::abs: {
          auto Op0 = getVal(Call->getArgOperand(0), ValueStack, Variables, Par);
          auto a = dyn_cast<ConstantInt>(Op0)->getZExtValue();
          auto r = __builtin_abs(a);
          InstResult = ConstantInt::get(Op0->getType(), r);
        } break;
        case Intrinsic::cttz: {
          auto Op0 = getVal(Call->getArgOperand(0), ValueStack, Variables, Par);
          auto a = dyn_cast<ConstantInt>(Op0)->getZExtValue();
          auto r = __builtin_ctz(a);
          InstResult = ConstantInt::get(Op0->getType(), r);
        } break;
        default: {
          CI->dump();
          outs() << "getIntrinsicID: " << CI->getIntrinsicID() << "\n";
          report_fatal_error("[!] Not supported intrinsic!", false);
        }
      }
    } else {
      CurInst->dump();
      report_fatal_error("[!] Not supported instruction!", false);
    }

  Done:
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

llvm::Constant *LLVMParser::getVal(
    llvm::Value *V,
    llvm::SmallDenseMap<llvm::Value *, llvm::Constant *, 16> &ValueStack,
    llvm::SmallVectorImpl<llvm::Value *> &Variables,
    llvm::SmallVectorImpl<llvm::APInt> &Par) {
  if (Constant *CV = dyn_cast<Constant>(V)) return CV;

  // Check if variable
  int i = 0;
  for (auto Var : Variables) {
    if (Var != V) {
      i++;
      continue;
    }

    if (V->getType()->isPointerTy()) {
      llvm::Type *Ty = Type::getIntNTy(V->getContext(), Par[i].getBitWidth());
      return getConstantInt(Ty, Par[i]);
    }

    // Check if Type is different
    if (Par[i].getBitWidth() > V->getType()->getIntegerBitWidth()) {
      return getConstantInt(V->getType(),
                            Par[i].trunc(V->getType()->getIntegerBitWidth()));
    } else if (Par[i].getBitWidth() < V->getType()->getIntegerBitWidth()) {
      return getConstantInt(V->getType(),
                            Par[i].zext(V->getType()->getIntegerBitWidth()));
    } else {
      return getConstantInt(V->getType(), Par[i]);
    }
  }

  if (ValueStack.count(V) == 0) {
    V->dump();
    report_fatal_error("V not found!");
  }

  return ValueStack[V];
}

llvm::Constant *LLVMParser::getConstantInt(llvm::Type *Ty, uint64_t Value) {
  auto C = llvm::ConstantInt::get(Ty, Value);
  return C;
}

llvm::Constant *LLVMParser::getConstantInt(llvm::Type *Ty, APInt Value) {
  auto C = llvm::ConstantInt::get(Ty, Value);
  return C;
}

bool LLVMParser::doesDominateInst(DominatorTree *DT, const Instruction *InstA,
                                  const Instruction *InstB) {
  if (InstA == InstB) return false;

  const BasicBlock *BA = InstA->getParent();
  const BasicBlock *BB = InstB->getParent();

  // Use ordered basic block in case the 2 instructions are in the same
  // block.
  if (BA == BB) return InstA->comesBefore(InstB);

  DomTreeNode *DA = DT->getNode(BA);
  DomTreeNode *DB = DT->getNode(BB);
  if (DA->getLevel() != DB->getLevel()) return DA->getLevel() < DB->getLevel();

  // Same dominator-tree level but different blocks. Neither instruction can
  // be an operand of the other (that would require one block to dominate the
  // other, which forces different levels), so any consistent order will do -
  // but it has to *be* an order. Reporting these as equivalent is what makes
  // this comparator not a strict weak ordering: for X and Y in one block and
  // Z in a sibling block at the same level, X and Y each compare equivalent
  // to Z while comparing strictly against each other, which breaks the
  // transitivity std::sort relies on. std::sort is then free to emit any
  // permutation, including one that places an instruction ahead of an
  // operand it needs - and evaluateAST walks the sorted AST assuming the
  // opposite, so getVal hits a value that is not a constant, not a
  // collected variable and not yet on the ValueStack, and aborts with
  // "V not found!". Tie-break on the block address (via std::less, which is
  // well defined for unrelated pointers) to make this a total order.
  return std::less<const BasicBlock *>{}(BA, BB);
}

// Neither z3::expr::bit2bool() nor the C API it wraps (Z3_mk_bit2bool) is
// available on every Z3 build this needs to compile against (both are
// missing under Ubuntu's libz3-dev) - build the same "is this bit set"
// Bool expression from extract() + equality instead, which every Z3
// version's API exposes.
static z3::expr bit2bool(const z3::expr &Bv, unsigned Idx) {
  return Bv.extract(Idx, Idx) == Bv.ctx().bv_val(1, 1);
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

    int IntBitWidth = 64;
    if (!V->getType()->isPointerTy()) {
      IntBitWidth = V->getType()->getIntegerBitWidth();
    }

    auto VExpr = Z3Ctx.bv_const(VarStr.c_str(), IntBitWidth);

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
    if (CurInst->getType()->isPointerTy()) {
      OverrideBitWidth = 64;
    } else {
      OverrideBitWidth = CurInst->getType()->getIntegerBitWidth();
    }

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
    } else if (auto GEP = dyn_cast<GetElementPtrInst>(CurInst)) {
      // Lower it as add
      auto exp =
          *getZ3Val(Z3Ctx, GEP->getOperand(0), ValueMAP, OverrideBitWidth) +
          *getZ3Val(Z3Ctx, GEP->getOperand(1), ValueMAP, OverrideBitWidth);
      ValueMAP[GEP] = new z3::expr(exp);
    } else if (auto SI = dyn_cast<SelectInst>(CurInst)) {
      // Select
      auto Cond = getZ3Val(Z3Ctx, SI->getCondition(), ValueMAP, false);
      auto VTrue = getZ3Val(Z3Ctx, SI->getTrueValue(), ValueMAP, false);
      auto VFalse = getZ3Val(Z3Ctx, SI->getFalseValue(), ValueMAP, false);

      // Get BitWidth
      int VTrueBitWidth = 0;
      if (SI->getTrueValue()->getType()->isPointerTy()) {
        VTrueBitWidth = 64;
      } else {
        VTrueBitWidth = SI->getTrueValue()->getType()->getIntegerBitWidth();
      }

      int VFalseBitWidth = 0;
      if (SI->getFalseValue()->getType()->isPointerTy()) {
        VFalseBitWidth = 64;
      } else {
        VFalseBitWidth = SI->getFalseValue()->getType()->getIntegerBitWidth();
      }

      int SIBitWidth = 0;
      if (SI->getTrueValue()->getType()->isPointerTy() ||
          SI->getFalseValue()->getType()->isPointerTy()) {
        SIBitWidth = OverrideBitWidth;
      } else {
        SIBitWidth = SI->getType()->getIntegerBitWidth();
      }

      // Cast to bool if needed
      if (Cond->get_sort().is_bool() == false) {
        Cond = new z3::expr(bit2bool(*Cond, 0));
      }

      // Cast bool to bv if needed
      if (VTrueBitWidth == 1 && VTrue->get_sort().is_bool() == false) {
        VTrue = new z3::expr(bit2bool(*VTrue, 0));
      }

      // Check is cast to bool is needed
      if (VFalseBitWidth == 1 && VFalse->get_sort().is_bool() == false) {
        VFalse = new z3::expr(bit2bool(*VFalse, 0));
      }

      auto Res = z3::ite(*Cond, *VTrue, *VFalse);

      ValueMAP[SI] = new z3::expr(boolToBV(Z3Ctx, Res, SIBitWidth));
    } else if (auto ICmp = dyn_cast<ICmpInst>(CurInst)) {
      // ICmp
      auto V0 = getZ3Val(Z3Ctx, ICmp->getOperand(0), ValueMAP, false);
      auto V1 = getZ3Val(Z3Ctx, ICmp->getOperand(1), ValueMAP, false);

      z3::expr *Res = nullptr;
      switch (ICmp->getPredicate()) {
        case llvm::ICmpInst::ICMP_EQ: {
          Res = new z3::expr(*V0 == *V1);
        } break;
        case llvm::ICmpInst::ICMP_NE:
          Res = new z3::expr(*V0 != *V1);
          break;
        case llvm::ICmpInst::ICMP_UGT:
          Res = new z3::expr(z3::ugt(*V0, *V1));
          break;
        case llvm::ICmpInst::ICMP_UGE:
          Res = new z3::expr(z3::uge(*V0, *V1));
          break;
        case llvm::ICmpInst::ICMP_ULT:
          Res = new z3::expr(z3::ult(*V0, *V1));
          break;
        case llvm::ICmpInst::ICMP_ULE:
          Res = new z3::expr(z3::ule(*V0, *V1));
          break;
        case llvm::ICmpInst::ICMP_SGT:
          Res = new z3::expr(z3::sgt(*V0, *V1));
          break;
        case llvm::ICmpInst::ICMP_SGE:
          Res = new z3::expr(z3::sge(*V0, *V1));
          break;
        case llvm::ICmpInst::ICMP_SLT:
          Res = new z3::expr(z3::slt(*V0, *V1));
          break;
        case llvm::ICmpInst::ICMP_SLE:
          Res = new z3::expr(z3::sle(*V0, *V1));
          break;
        default:
          report_fatal_error("Unsupported Predicate!", false);
      }

      ValueMAP[ICmp] = new z3::expr(
          boolToBV(Z3Ctx, *Res, CurInst->getType()->getIntegerBitWidth()));
    } else if (auto PTI = dyn_cast<PtrToIntInst>(CurInst)) {
      // Via getZ3Val, not ValueMAP[...] directly: the operand is not
      // necessarily an instruction already lowered into ValueMAP (these
      // casts get introduced around constants and function arguments in
      // getOptimizedZ3Expression). DenseMap::operator[] would silently
      // default-construct a null z3::expr* for such a key, and the
      // dereference below would then read the ast/context fields out of
      // address zero and hand the garbage to Z3_inc_ref.
      ValueMAP[PTI] = new z3::expr(
          *getZ3Val(Z3Ctx, PTI->getOperand(0), ValueMAP, OverrideBitWidth));
    } else if (auto ITP = dyn_cast<IntToPtrInst>(CurInst)) {
      ValueMAP[ITP] = new z3::expr(
          *getZ3Val(Z3Ctx, ITP->getOperand(0), ValueMAP, OverrideBitWidth));
    } else if (auto Call = dyn_cast<CallInst>(CurInst)) {
      auto CI = Call->getCalledFunction();
      switch (CI->getIntrinsicID()) {
        case Intrinsic::fshl: {
          // Implement as rotate left algorithm
          auto a = getZ3Val(Z3Ctx, Call->getArgOperand(0), ValueMAP, false);
          auto b = getZ3Val(Z3Ctx, Call->getArgOperand(1), ValueMAP, false);
          auto c = getZ3Val(Z3Ctx, Call->getArgOperand(2), ValueMAP, false);

          auto width = a->get_sort().bv_size();
          auto expr_width = z3::expr(Z3Ctx.bv_val(width, width));

          // c mod width
          auto c_mod_width = new z3::expr(*c % expr_width);

          // Rotate left
          auto r = z3::shl(*a, *c_mod_width) |
                   z3::lshr(*b, (expr_width - *c_mod_width));

          // Set result
          ValueMAP[Call] = new z3::expr(r);
        } break;
        case Intrinsic::fshr: {
          // Implement as rotate right algorithm
          auto a = getZ3Val(Z3Ctx, Call->getArgOperand(0), ValueMAP, false);
          auto b = getZ3Val(Z3Ctx, Call->getArgOperand(1), ValueMAP, false);
          auto c = getZ3Val(Z3Ctx, Call->getArgOperand(2), ValueMAP, false);

          auto width = a->get_sort().bv_size();
          auto expr_width = z3::expr(Z3Ctx.bv_val(width, width));

          // c mod width
          auto c_mod_width = new z3::expr(*c % expr_width);

          // Rotate right
          auto r = z3::lshr(*a, *c_mod_width) |
                   z3::shl(*b, (expr_width - *c_mod_width));

          // Set result
          ValueMAP[Call] = new z3::expr(r);
        } break;
        case Intrinsic::bitreverse: {
          // Reverse the order of bits
          auto Op0 = getZ3Val(Z3Ctx, Call->getArgOperand(0), ValueMAP, false);
          auto BitWidth =
              Call->getArgOperand(0)->getType()->getIntegerBitWidth();

          auto v = z3::expr_vector(Z3Ctx);
          // i--, not i++: counting up from BitWidth-1 never reaches the
          // i >= 0 exit, so this ran away extracting bits past the end of
          // the vector until it exhausted memory.
          for (int i = BitWidth - 1; i >= 0; i--) {
            v.push_back(Op0->extract(i, i));
          }
          auto r = concat(v);

          ValueMAP[Call] = new z3::expr(r);
        } break;
        case Intrinsic::ctpop: {
          auto Op0 = getZ3Val(Z3Ctx, Call->getArgOperand(0), ValueMAP, false);
          auto BitWidth =
              Call->getArgOperand(0)->getType()->getIntegerBitWidth();

          auto temp = z3::zext(Op0->extract(0, 0), BitWidth - 1);
          for (int i = 1; i < BitWidth; i++) {
            temp = temp + z3::zext(Op0->extract(i, i), BitWidth - 1);
          }

          ValueMAP[Call] = new z3::expr(temp);
        } break;
        case Intrinsic::bswap: {
          auto Op0 = getZ3Val(Z3Ctx, Call->getArgOperand(0), ValueMAP, false);
          auto BitWidth =
              Call->getArgOperand(0)->getType()->getIntegerBitWidth();

          auto v = z3::expr_vector(Z3Ctx);
          for (int i = (BitWidth / 8) - 1; i >= 0; i--) {
            v.push_back(
                Op0->extract(BitWidth - (8 * i) - 1, BitWidth - (8 * (i + 1))));
          }

          auto temp = z3::concat(v);

          ValueMAP[Call] = new z3::expr(temp);
        } break;
        case Intrinsic::umax: {
          auto Op0 = getZ3Val(Z3Ctx, Call->getArgOperand(0), ValueMAP, false);
          auto Op1 = getZ3Val(Z3Ctx, Call->getArgOperand(1), ValueMAP, false);

          ValueMAP[Call] =
              new z3::expr(z3::ite(z3::ule(*Op0, *Op1), *Op1, *Op0));
        } break;
        case Intrinsic::umin: {
          auto Op0 = getZ3Val(Z3Ctx, Call->getArgOperand(0), ValueMAP, false);
          auto Op1 = getZ3Val(Z3Ctx, Call->getArgOperand(1), ValueMAP, false);

          ValueMAP[Call] =
              new z3::expr(z3::ite(z3::ule(*Op0, *Op1), *Op0, *Op1));
        } break;
        case Intrinsic::smin: {
          auto Op0 = getZ3Val(Z3Ctx, Call->getArgOperand(0), ValueMAP, false);
          auto Op1 = getZ3Val(Z3Ctx, Call->getArgOperand(1), ValueMAP, false);

          ValueMAP[Call] =
              new z3::expr(z3::ite(z3::sle(*Op0, *Op1), *Op0, *Op1));
        } break;
        case Intrinsic::smax: {
          auto Op0 = getZ3Val(Z3Ctx, Call->getArgOperand(0), ValueMAP, false);
          auto Op1 = getZ3Val(Z3Ctx, Call->getArgOperand(1), ValueMAP, false);

          ValueMAP[Call] =
              new z3::expr(z3::ite(z3::sle(*Op0, *Op1), *Op1, *Op0));
        } break;
        case Intrinsic::abs: {
          auto Op0 = getZ3Val(Z3Ctx, Call->getArgOperand(0), ValueMAP, false);
          ValueMAP[Call] = new z3::expr(z3::abs(*Op0));
        } break;
        case Intrinsic::cttz: {
          auto Op0 = getZ3Val(Z3Ctx, Call->getArgOperand(0), ValueMAP, false);
          unsigned BitWidth = Op0->get_sort().bv_size();

          // Z3's bitvector theory has no native count-trailing-zeros -
          // build it as a chain of ITEs checking each bit from the LSB
          // upward, matching evaluateAST's own __builtin_ctz semantics
          // (used when this same AST is concretely evaluated rather than
          // symbolically proven) so both paths agree. An all-zero input
          // is UB for __builtin_ctz too, so falling back to BitWidth here
          // is as good a definition as any for that case.
          auto Result = Z3Ctx.bv_val((uint64_t)BitWidth, BitWidth);
          for (int i = (int)BitWidth - 1; i >= 0; i--) {
            auto BitSet = Op0->extract(i, i) == Z3Ctx.bv_val(1, 1);
            Result = z3::ite(BitSet, Z3Ctx.bv_val((uint64_t)i, BitWidth),
                             Result);
          }

          ValueMAP[Call] = new z3::expr(Result);
        } break;
        case Intrinsic::usub_sat: {
          auto Op0 = getZ3Val(Z3Ctx, Call->getArgOperand(0), ValueMAP, false);
          auto Op1 = getZ3Val(Z3Ctx, Call->getArgOperand(1), ValueMAP, false);

          ValueMAP[Call] = new z3::expr(
              z3::ite(z3::ule(*Op0, *Op1),
                      Z3Ctx.bv_val(0, Op0->get_sort().bv_size()), *Op0 - *Op1));
        } break;
        default: {
          CI->dump();
          report_fatal_error("[!] Not supported call instruction!", false);
        }
      }
    } else {
      CurInst->dump();
      report_fatal_error("[!] Not supported instruction in Z3 parser!", false);
    }

    // Set last inst
    LastInst = ValueMAP[CurInst];
  }

  // Every case above either sets ValueMAP[CurInst] or report_fatal_errors
  // out - but DenseMap::operator[] silently default-constructs (null) an
  // entry for a key it's never seen, so a case that matches syntactically
  // but leaves ValueMAP unset for its own result (as the cttz intrinsic
  // case used to, before it was implemented) would otherwise surface here
  // as a null LastInst - dereferenced two lines down without this check,
  // crashing inside Z3's own reference counting on a value that looks
  // like ordinary heap garbage rather than a recognizable null pointer.
  if (!LastInst) {
    report_fatal_error(
        "[getZ3ExpressionFromAST] LastInst is null - AST root never "
        "resolved to a value",
        false);
  }

  z3::expr Result = *LastInst;

  // Check for double entries
  SmallPtrSet<void *, 8> Contains;

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

    if (Found) continue;

    // Dont delete twice, can happen for constants
    if (Contains.contains(V.second) == false) {
      delete V.second;

      Contains.insert(V.second);
    }
  }

  return Result;
}

z3::expr LLVMParser::boolToBV(z3::context &Z3Ctx, z3::expr &BoolExpr,
                              int BitWidth) {
  // Do nothing if already bv
  if (!BoolExpr.get_sort().is_bool()) {
    return BoolExpr;
  }

  auto One = Z3Ctx.bv_val(1, BitWidth);
  auto Zero = Z3Ctx.bv_val(0, BitWidth);

  return z3::ite(BoolExpr, One, Zero);
}

z3::expr *LLVMParser::getZ3Val(
    z3::context &Z3Ctx, llvm::Value *V,
    llvm::DenseMap<llvm::Value *, z3::expr *> &ValueMap, int OverrideBitWidth) {
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

  //  Check if IntToPtr
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(V)) {
    if (CE->getOpcode() == Instruction::IntToPtr) {
      auto Z3Val = getZ3Val(Z3Ctx, CE->getOperand(0), ValueMap, 64);
      ValueMap[V] = Z3Val;
      return ValueMap[V];
    }
  }

  // Check Null Ptr
  if (Constant *C = dyn_cast<Constant>(V)) {
    if (C->isNullValue()) {
      auto ConstExpr = Z3Ctx.bv_val(0, 64);
      ValueMap[V] = new z3::expr(ConstExpr);

      return ValueMap[V];
    }
  }

  // Check if it already exists
  if (ValueMap.count(V) == 0) {
    outs() << "\nValue:";
    V->dump();
    V->getType()->dump();
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

llvm::Function *LLVMParser::getASTasLLVMFunction(
    llvm::Module *M, llvm::SmallVectorImpl<BFSEntry> &AST,
    llvm::SmallVectorImpl<llvm::Value *> &Variables) {
  // Create new function
  std::vector<llvm::Type *> ArgsTy;
  for (int i = 0; i < Variables.size(); i++) {
    ArgsTy.push_back(Variables[i]->getType());
  }

  auto RetType = AST.begin()->I->getType();

  auto FTy = llvm::FunctionType::get(RetType, ArgsTy, false);
  auto F = llvm::Function::Create(
      FTy, llvm::GlobalValue::LinkageTypes::ExternalLinkage, "MBA_Simp", *M);

  // Create new BB
  auto *BB = llvm::BasicBlock::Create(M->getContext(), "MBA_BB", F);

  // Create the builder to build
  llvm::IRBuilder<> Builder(BB);
  // Map vars
  SmallVector<llvm::Value *, 4> FArgs;
  std::map<llvm::Value *, llvm::Value *> VarMap;
  int i = 0;
  for (auto &V : Variables) {
    auto A = F->getArg(i);
    VarMap[V] = A;
    FArgs.push_back(A);
    i++;
  }

  // Clone AST instructions into F
  Instruction *LastInst = nullptr;
  for (auto E = AST.rbegin(); E != AST.rend(); ++E) {
    auto &e = *E;

    // Clone inst
    auto NewI = e.I->clone();

    // Replace operands
    for (auto &Op : NewI->operands()) {
      if (VarMap.count(Op)) {
        // Cast to correct type
        auto OpType = Op->getType();
        auto VarOpType = VarMap[Op]->getType();
        if (OpType != VarOpType) {
          Op = CastInst::CreateIntegerCast(VarMap[Op], OpType, true, "",
                                           &F->getEntryBlock());
        } else {
          Op = VarMap[Op];
        }
      }
    }

    // Insert instruction
    NewI->insertAfter(&F->getEntryBlock().back());

    // Update VarMap
    VarMap[e.I] = NewI;
    LastInst = NewI;
  }

  // Create return
  auto RetInst = ReturnInst::Create(M->getContext(), LastInst, BB);

  return F;
}

z3::expr LLVMParser::getOptimizedZ3Expression(
    z3::context &Z3Ctx, std::string &SimpExpr, std::vector<std::string> &VNames,
    llvm::SmallVectorImpl<BFSEntry> &AST,
    llvm::SmallVectorImpl<llvm::Value *> &Variables, OPTSTATUS &Proved) {
  // Create function from simplified expression
  auto F = createLLVMFunction(this->M, Variables, SimpExpr, VNames,
                              AST.begin()->I->getType());

  // Subtract candidate from return value
  auto RetInst = dyn_cast<ReturnInst>(F->getEntryBlock().getTerminator());
  auto V = RetInst->getReturnValue();
  auto Int64Ty = Type::getInt64Ty(V->getContext());

  // Map vars
  SmallVector<llvm::Value *, 4> FArgs;
  std::map<llvm::Value *, llvm::Value *> VarMap;
  int i = 0;
  for (auto &V : Variables) {
    auto A = F->getArg(i);
    VarMap[V] = A;
    FArgs.push_back(A);
    i++;
  }

  // Clone AST instructions into F
  Instruction *LastInst = nullptr;
  for (auto E = AST.rbegin(); E != AST.rend(); ++E) {
    auto &e = *E;

    // Clone inst
    auto NewI = e.I->clone();

    // Replace operands
    for (auto &Op : NewI->operands()) {
      if (VarMap.count(Op)) {
        // Cast to correct type
        auto OpType = Op->getType();
        auto VarOpType = VarMap[Op]->getType();
        if (OpType != VarOpType) {
          Op = CastInst::CreateIntegerCast(VarMap[Op], OpType, true, "",
                                           F->getEntryBlock().getTerminator());
        } else {
          Op = VarMap[Op];
        }
      }
    }

    // Insert instruction
    NewI->insertBefore(F->getEntryBlock().getTerminator());

    // Update VarMap
    VarMap[e.I] = NewI;
    LastInst = NewI;
  }

  // Subtract Candidate from Replacement

  // Cast LastInst to correct type
  auto LastInstType = LastInst->getType();
  if (LastInstType != V->getType()) {
    LastInst = CastInst::CreateIntegerCast(LastInst, V->getType(), true, "",
                                           F->getEntryBlock().getTerminator());
  }

  // Cast Ptr to Int if needed
  if (V->getType()->isPointerTy()) {
    V = new PtrToIntInst(dyn_cast<Instruction>(V), Int64Ty, "",
                         F->getEntryBlock().getTerminator());
  }

  if (LastInstType->isPointerTy()) {
    LastInst = new PtrToIntInst(dyn_cast<Instruction>(LastInst), Int64Ty, "",
                                F->getEntryBlock().getTerminator());
  }

  llvm::Instruction *NewI = BinaryOperator::CreateSub(V, LastInst);
  NewI->insertBefore(F->getEntryBlock().getTerminator());

  // Replace return value
  // Cast to correct type
  auto RetType = RetInst->getReturnValue()->getType();
  if (!RetType->isPointerTy() && RetType != NewI->getType()) {
    NewI = CastInst::CreateIntegerCast(NewI, RetType, true, "",
                                       F->getEntryBlock().getTerminator());
  }

  // Cast to back to Ptr if needed
  if (RetType->isPointerTy() && !NewI->getType()->isPointerTy()) {
    NewI =
        new IntToPtrInst(NewI, RetType, "", F->getEntryBlock().getTerminator());
  }

  RetInst->setOperand(0, NewI);

  // Now optimize
  optimizeFunction(*F);

  // check if proved
  Proved = OPT_PROVE_ME;
  if (F->getEntryBlock().size() == 1) {
    if (auto C = dyn_cast<ConstantInt>(RetInst->getReturnValue())) {
      if (C->isZero()) {
        Proved = OPT_PROVED;
      } else {
        Proved = OPT_NOT_VALID;
      }
    } else if (RetInst->getReturnValue()->getType()->isPointerTy()) {
      if (auto C = dyn_cast<ConstantPointerNull>(RetInst->getReturnValue())) {
        Proved = OPT_PROVED;
      } else {
        Proved = OPT_NOT_VALID;
      }
    }

    F->eraseFromParent();

    return z3::expr(Z3Ctx.bool_val(true));
  }

  // Get Z3 expression
  SmallVector<BFSEntry, 4> OptAST;
  i = 0;
  for (auto I = ++F->getEntryBlock().rbegin(), E = F->getEntryBlock().rend();
       I != E; ++I) {
    OptAST.push_back(BFSEntry(i++, &*I));
  }

  std::map<std::string, z3::expr *> Z3VarMap;
  auto BitWidth = 64;
  if (!F->getReturnType()->isPointerTy()) {
    BitWidth = F->getReturnType()->getIntegerBitWidth();
  }

  auto Z3ExpOpt =
      getZ3ExpressionFromAST(Z3Ctx, OptAST, FArgs, Z3VarMap, BitWidth);

  // getZ3ExpressionFromAST's own cleanup deliberately skips entries that
  // match Z3VarMap's values (variables), leaving them for the caller to
  // own/reuse. This is the only caller, and Z3VarMap is function-local and
  // never consulted again after this point, so nothing reuses these - free
  // them here rather than leaking one z3::expr (and its Z3_inc_ref'd AST
  // node) per call. Z3ExpOpt already holds its own independently
  // ref-counted copy, so this is safe regardless of whether it aliases one
  // of these. A real leak either way - one node per verify() attempt,
  // against a single shared, process-lifetime Z3 context (Z3CtxGlobal) -
  // even though it wasn't the cause of the specific denuvomaximum crash
  // this was investigated alongside (confirmed by testing: the crash
  // still reproduced identically with this fix alone applied).
  for (auto &Entry : Z3VarMap) {
    delete Entry.second;
  }

  // Clean up
  F->eraseFromParent();

  return Z3ExpOpt;
}

void LLVMParser::optimizeFunction(llvm::Function &F) {
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PassBuilder PB;

  // Register all the basic analyses with the managers.
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  FunctionPassManager FPM = PB.buildFunctionSimplificationPipeline(
      OptimizationLevel::O3, ThinOrFullLTOPhase::None);

  FPM.run(F, FAM);
}

}  // namespace LSiMBA
