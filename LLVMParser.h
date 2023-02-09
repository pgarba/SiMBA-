#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#pragma warning(disable : 4146)
#pragma warning(disable : 4624)
#pragma warning(disable : 4530)

#ifndef LLVMPARSER_H
#define LLVMPARSER_H

#include <unordered_set>

#include "llvm/IR/Instructions.h"

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
  llvm::BinaryOperator *Candidate;

  llvm::SmallVector<BFSEntry, 16> AST;

  llvm::SmallVector<llvm::Value *, 8> Variables;

  std::string Replacement;

  bool isValid = false;
} MBACandidate;

class LLVMParser {
public:
  LLVMParser(const std::string &filename, const std::string &OutputFile,
             int BitWidth = 64, bool Signed = true, bool Parallel = true,
             bool Verify = true, bool OptimizeBefore = true,
             bool OptimizeAfter = true, bool Debug = false);
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

private:
  std::string OutputFile = "";

  int BitWidth;

  bool Signed;

  bool Parallel;

  bool Verify;

  bool OptimizeBefore;

  bool OptimizeAfter;

  bool Debug;

  int MaxThreadCount;

  static llvm::LLVMContext Context;

  llvm::TargetLibraryInfoImpl *TLII;

  std::unique_ptr<llvm::TargetLibraryInfo> TLI;

  std::unique_ptr<llvm::Module> M;

  SplitMix64 SP64;

  std::unique_ptr<llvm::Evaluator> Eval;

  int extractAndSimplify();

  int simplifyMBAModule();

  void writeModule();

  bool parse(const std::string &filename);

  bool verify(llvm::Function *F0, llvm::Function *F1, uint64_t Modulus);

  bool verify(llvm::SmallVectorImpl<BFSEntry> &AST, std::string &SimpExpr,
              llvm::SmallVectorImpl<llvm::Value *> &Variables);

  bool verify_parallel(llvm::Function *F0, llvm::Function *F1,
                       uint64_t Modulus);

  llvm::Instruction *getSingleTerminator(llvm::Function &F);

  bool hasLoadStores(llvm::Function &F);

  void initResultVector(llvm::Function &F, std::vector<int64_t> &ResultVector,
                        uint64_t Modulus, int VNumber, llvm::Type *IntType);

  bool runLLVMOptimizer(bool Initial = false);

  void extractCandidates(llvm::Function &F,
                         std::vector<MBACandidate> &Candidates);

  bool findReplacements(llvm::DominatorTree *DT,
                        std::vector<MBACandidate> &Candidates);

  void getAST(llvm::DominatorTree *DT, llvm::Instruction *I,
              llvm::SmallVectorImpl<BFSEntry> &AST,
              llvm::SmallVectorImpl<llvm::Value *> &Variables, bool KeepRoot);

  bool walkSubAST(llvm::DominatorTree *DT, llvm::SmallVectorImpl<BFSEntry> &AST,
                  std::vector<MBACandidate> &Candidates);

  void printAST(llvm::SmallVectorImpl<BFSEntry> &AST, bool isRootAST);

  void initResultVectorFromAST(llvm::SmallVectorImpl<BFSEntry> &AST,
                               std::vector<int64_t> &ResultVector,
                               uint64_t Modulus,
                               llvm::SmallVectorImpl<llvm::Value *> &Variables);

  int64_t evaluateAST(llvm::SmallVectorImpl<BFSEntry> &AST,
                      llvm::SmallVectorImpl<llvm::Value *> &Variables,
                      llvm::SmallVectorImpl<int64_t> &Par);

  llvm::Constant *
  getVal(llvm::Value *V,
         llvm::DenseMap<llvm::Value *, llvm::Constant *> &ValueStack,
         llvm::SmallVectorImpl<llvm::Value *> &Variables,
         llvm::SmallVectorImpl<int64_t> &Par);

  bool doesDominateInst(llvm::DominatorTree *DT, const llvm::Instruction *InstA,
                        const llvm::Instruction *InstB);
};

} // namespace LSiMBA

#endif // LLVMPARSER_H