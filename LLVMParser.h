#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#pragma warning(disable : 4146)
#pragma warning(disable : 4624)
#pragma warning(disable : 4530)

#ifndef LLVMPARSER_H
#define LLVMPARSER_H

#include <map>
#include <unordered_set>

#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/IR/Instructions.h>

#include <z3++.h>

#include "splitmix64.h"

namespace llvm {
class DominatorTree;
class Function;
class Module;
class LLVMContext;
class Evaluator;
class TargetLibraryInfoImpl;
class TargetLibraryInfo;
class Type;
}  // namespace llvm

namespace LSiMBA {

enum OPTSTATUS {
  OPT_PROVED = 0,
  OPT_NOT_VALID = 1,
  OPT_PROVE_ME = 2,
};

typedef struct BFSEntry {
  int Depth;
  llvm::Instruction *I;

  BFSEntry(int Depth, llvm::Instruction *I) : Depth(Depth), I(I) {};
} BFSEntry;

typedef struct MBACandidate {
  llvm::Instruction *Candidate;

  int ASTSize = 0;
  llvm::SmallVector<BFSEntry, 16> AST;

  llvm::SmallVector<llvm::Value *, 8> Variables;

  std::string Replacement;

  bool isValid = false;
} MBACandidate;

class LLVMParser {
 public:
  LLVMParser(const std::string &filename, const std::string &OutputFile,
             bool Parallel = true, bool Verify = true,
             bool OptimizeBefore = false, bool OptimizeAfter = true,
             bool Debug = false, bool Prove = false);

  LLVMParser(llvm::Module *M, bool Parallel = true, bool Verify = true,
             bool OptimizeBefore = false, bool OptimizeAfter = true,
             bool Debug = false, bool Prove = false);

  LLVMParser(llvm::Function *F, bool Parallel = true, bool Verify = true,
             bool OptimizeBefore = false, bool OptimizeAfter = true,
             bool Debug = false, bool Prove = false);

  ~LLVMParser();

  /*
    Searches for MBAs inside the function and tries to simplify all MBAs
  */
  int simplify();

  /*
    Interprets the function as one MBA and tries to simplify it
  */
  int simplifyMBAFunctionsOnly();

  /*
    Get the current LLVM Context
  */
  static llvm::LLVMContext &getLLVMContext();

  /*
    Get the instruction count before simplification
  */
  int getInstructionCountBefore();

  /*
    Get the instruction count after simplification
  */
  int getInstructionCountAfter();

  /*
    Get AST as LLVM Function
  */
  llvm::Function *getASTasLLVMFunction(
      llvm::Module *M, llvm::SmallVectorImpl<BFSEntry> &AST,
      llvm::SmallVectorImpl<llvm::Value *> &Variables);

  /*
    If simplifcation fails try to replace with known patterns
  */
  bool replaceWithKnownPatterns(LSiMBA::MBACandidate &Cand,
                                const std::vector<llvm::APInt> &ResultVector);

 private:
  std::string OutputFile = "";

  bool Parallel;

  bool Verify;

  bool Prove;

  bool OptimizeBefore;

  bool OptimizeAfter;

  bool Debug;

  int MaxThreadCount;

  bool IsExternalSimplifier = false;

  bool CountInstructions = true;

  int InstructionCountBefore = 0;

  int InstructionCountAfter = 0;

  static llvm::LLVMContext Context;

  llvm::TargetLibraryInfoImpl *TLII;

  std::unique_ptr<llvm::TargetLibraryInfo> TLI;

  llvm::Module *M;

  llvm::Function *F;

  SplitMix64 SP64;

  std::unique_ptr<llvm::Evaluator> Eval;

  int extractAndSimplify();

  int simplifyMBAModule();

  void writeModule();

  bool parse(const std::string &filename);

  bool verify(llvm::Function *F0, llvm::Function *F1, llvm::APInt &Modulus);

  bool verify(int ASTSize, llvm::SmallVectorImpl<BFSEntry> &AST,
              std::string &SimpExpr,
              llvm::SmallVectorImpl<llvm::Value *> &Variables, int BitWidth);

  llvm::Instruction *getSingleTerminator(llvm::Function &F);

  bool hasLoadStores(llvm::Function &F);

  void initResultVector(llvm::Function &F,
                        std::vector<llvm::APInt> &ResultVector,
                        const llvm::APInt &Modulus, int VNumber,
                        llvm::Type *IntType);

  void optimizeFunction(llvm::Function &F);

  bool isSupportedInstruction(llvm::Value *V);

  void extractCandidates(llvm::Function &F,
                         std::vector<MBACandidate> &Candidates);

  bool constainsReplacedInstructions(
      llvm::SmallPtrSet<llvm::Instruction *, 16> &ReplacedInstructions,
      MBACandidate &Cand);

  bool findReplacements(llvm::DominatorTree *DT,
                        std::vector<MBACandidate> &Candidates);

  void getAST(llvm::DominatorTree *DT, llvm::Instruction *I,
              llvm::SmallVectorImpl<BFSEntry> &AST,
              llvm::SmallVectorImpl<llvm::Value *> &Variables, bool KeepRoot);

  int getASTSize(llvm::SmallVectorImpl<BFSEntry> &AST);

  bool walkSubAST(llvm::DominatorTree *DT, llvm::SmallVectorImpl<BFSEntry> &AST,
                  std::vector<MBACandidate> &Candidates);

  void printAST(llvm::SmallVectorImpl<BFSEntry> &AST);

  uint64_t calculateHash(llvm::SmallVectorImpl<BFSEntry> &AST);

  std::string getASTAsString(llvm::SmallVectorImpl<BFSEntry> &AST,
                             llvm::SmallVectorImpl<llvm::Value *> &Variables);

  void initResultVectorFromAST(llvm::SmallVectorImpl<BFSEntry> &AST,
                               std::vector<llvm::APInt> &ResultVector,
                               const llvm::APInt &Modulus,
                               llvm::SmallVectorImpl<llvm::Value *> &Variables,
                               int BitWidth);

  llvm::APInt evaluateAST(llvm::SmallVectorImpl<BFSEntry> &AST,
                          llvm::SmallVectorImpl<llvm::Value *> &Variables,
                          llvm::SmallVectorImpl<llvm::APInt> &Par, bool &Error);

  llvm::Constant *getVal(
      llvm::Value *V,
      llvm::DenseMap<llvm::Value *, llvm::Constant *> &ValueStack,
      llvm::SmallVectorImpl<llvm::Value *> &Variables,
      llvm::SmallVectorImpl<llvm::APInt> &Par);

  /*
    Thread safe
  */
  llvm::Constant *getConstantInt(llvm::Type *Ty, uint64_t Value);
  llvm::Constant *getConstantInt(llvm::Type *Ty, llvm::APInt Value);

  bool doesDominateInst(llvm::DominatorTree *DT, const llvm::Instruction *InstA,
                        const llvm::Instruction *InstB);

  z3::expr getZ3ExpressionFromAST(
      z3::context &Z3Ctx, llvm::SmallVectorImpl<BFSEntry> &AST,
      llvm::SmallVectorImpl<llvm::Value *> &Variables,
      std::map<std::string, z3::expr *> &VarMap, int OverrideBitWidth = 0);

  z3::expr getOptimizedZ3Expression(
      z3::context &Z3Ctx, std::string &SimpExpr,
      std::vector<std::string> &VNames, llvm::SmallVectorImpl<BFSEntry> &AST,
      llvm::SmallVectorImpl<llvm::Value *> &Variables, OPTSTATUS &Proved);

  z3::expr boolToBV(z3::context &Z3Ctx, z3::expr &BoolExpr, int BitWidth);

  z3::expr *getZ3Val(z3::context &Z3Ctx, llvm::Value *V,
                     llvm::DenseMap<llvm::Value *, z3::expr *> &ValueMap,
                     int OverrideBitWidth = 0);

  uint64_t getMASK(llvm::Type *Ty);

  static int getInstructionCount(llvm::Module *M);
  static int getInstructionCount(llvm::Function *F);

  int countVariables(std::string &expr, char Var);
};

}  // namespace LSiMBA

#endif  // LLVMPARSER_H
