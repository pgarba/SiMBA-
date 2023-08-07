#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#pragma warning(disable : 4146)
#pragma warning(disable : 4624)
#pragma warning(disable : 4530)

#ifndef LLVMPARSER_H
#define LLVMPARSER_H

#include <map>
#include <unordered_set>

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
} // namespace llvm

namespace LSiMBA {

typedef struct BFSEntry {
  int Depth;
  llvm::Instruction *I;

  BFSEntry(int Depth, llvm::Instruction *I) : Depth(Depth), I(I){};
} BFSEntry;

typedef struct MBACandidate {
  llvm::Instruction *Candidate;

  llvm::SmallVector<BFSEntry, 16> AST;

  llvm::SmallVector<llvm::Value *, 8> Variables;

  std::string Replacement;

  bool isValid = false;
} MBACandidate;

class LLVMParser {
public:
  LLVMParser(const std::string &filename, const std::string &OutputFile,
             bool Parallel = true, bool Verify = true,
             bool OptimizeBefore = true, bool OptimizeAfter = true,
             bool Debug = false, bool Prove = false);

  LLVMParser(llvm::Module *M, bool Parallel = true, bool Verify = true,
             bool OptimizeBefore = true, bool OptimizeAfter = true,
             bool Debug = false, bool Prove = false);

  LLVMParser(llvm::Function *F, bool Parallel = true, bool Verify = true,
             bool OptimizeBefore = true, bool OptimizeAfter = true,
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

  static llvm::LLVMContext &getLLVMContext();

  int getInstructionCountBefore();

  int getInstructionCountAfter();

private:
  std::string OutputFile = "";

  bool Parallel;

  bool Verify;

  bool Prove;

  bool OptimizeBefore;

  bool OptimizeAfter;

  bool Debug;

  int MaxThreadCount;

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

  bool verify(llvm::SmallVectorImpl<BFSEntry> &AST, std::string &SimpExpr,
              llvm::SmallVectorImpl<llvm::Value *> &Variables);

  llvm::Instruction *getSingleTerminator(llvm::Function &F);

  bool hasLoadStores(llvm::Function &F);

  void initResultVector(llvm::Function &F,
                        std::vector<llvm::APInt> &ResultVector,
                        const llvm::APInt &Modulus, int VNumber,
                        llvm::Type *IntType);

  bool runLLVMOptimizer(bool Initial = false);

  bool rewriteIntrinsics();

  bool isSupportedInstruction(llvm::Value *V);

  void extractCandidates(llvm::Function &F,
                         std::vector<MBACandidate> &Candidates);

  bool findReplacements(llvm::DominatorTree *DT,
                        std::vector<MBACandidate> &Candidates);

  void getAST(llvm::DominatorTree *DT, llvm::Instruction *I,
              llvm::SmallVectorImpl<BFSEntry> &AST,
              llvm::SmallVectorImpl<llvm::Value *> &Variables, bool KeepRoot);

  bool walkSubAST(llvm::DominatorTree *DT, llvm::SmallVectorImpl<BFSEntry> &AST,
                  std::vector<MBACandidate> &Candidates);

  void printAST(llvm::SmallVectorImpl<BFSEntry> &AST);

  std::string getASTAsString(llvm::SmallVectorImpl<BFSEntry> &AST,
                             llvm::SmallVectorImpl<llvm::Value *> &Variables);

  void initResultVectorFromAST(llvm::SmallVectorImpl<BFSEntry> &AST,
                               std::vector<llvm::APInt> &ResultVector,
                               const llvm::APInt &Modulus,
                               llvm::SmallVectorImpl<llvm::Value *> &Variables);

  llvm::APInt evaluateAST(llvm::SmallVectorImpl<BFSEntry> &AST,
                          llvm::SmallVectorImpl<llvm::Value *> &Variables,
                          llvm::SmallVectorImpl<llvm::APInt> &Par, bool &Error);

  llvm::Constant *
  getVal(llvm::Value *V,
         llvm::DenseMap<llvm::Value *, llvm::Constant *> &ValueStack,
         llvm::SmallVectorImpl<llvm::Value *> &Variables,
         llvm::SmallVectorImpl<llvm::APInt> &Par);

  bool doesDominateInst(llvm::DominatorTree *DT, const llvm::Instruction *InstA,
                        const llvm::Instruction *InstB);

  z3::expr getZ3ExpressionFromAST(
      z3::context &Z3Ctx, llvm::SmallVectorImpl<BFSEntry> &AST,
      llvm::SmallVectorImpl<llvm::Value *> &Variables,
      std::map<std::string, z3::expr *> &VarMap, int OverrideBitWidth = 0);

  z3::expr *getZ3Val(z3::context &Z3Ctx, llvm::Value *V,
                     llvm::DenseMap<llvm::Value *, z3::expr *> &ValueMap,
                     int OverrideBitWidth = 0);

  uint64_t getMASK(llvm::Type *Ty);

  static int getInstructionCount(llvm::Module *M);
};

} // namespace LSiMBA

#endif // LLVMPARSER_H