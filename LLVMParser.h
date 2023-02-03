#ifndef LLVMPARSER_H
#define LLVMPARSER_H

#include "splitmix64.h"

#include "llvm/IR/LLVMContext.h"

namespace llvm {
class Function;
class Module;
class LLVMContext;
class Evaluator;
class TargetLibraryInfoImpl;
class TargetLibraryInfo;
class Type;
} // namespace llvm

namespace LSiMBA {

class LLVMParser {
public:
  LLVMParser(const std::string &filename, int BitWidth = 64, bool Signed = true,
             bool Parallel = true, bool Verify = true,
             bool OptimizeBefore = true, bool OptimizeAfter = true);
  ~LLVMParser();

  int simplify();

  static llvm::LLVMContext &getLLVMContext();

private:
  int BitWidth;

  bool Signed;

  bool Parallel;

  bool Verify;

  bool OptimizeBefore;

  bool OptimizeAfter;

  int MaxThreadCount;

  static llvm::LLVMContext Context;

  std::unique_ptr<llvm::TargetLibraryInfoImpl> TLII;

  std::unique_ptr<llvm::TargetLibraryInfo> TLI;

  std::unique_ptr<llvm::Module> M;

  SplitMix64 SP64;

  std::unique_ptr<llvm::Evaluator> Eval;

  bool parse(const std::string &filename);

  int simplifyModule();

  bool verify(llvm::Function *F0, llvm::Function *F1, uint64_t Modulus);

  bool verify_parallel(llvm::Function *F0, llvm::Function *F1,
                       uint64_t Modulus);

  llvm::Instruction *getSingleTerminator(llvm::Function &F);

  bool hasLoadStores(llvm::Function &F);

  void initResultVector(llvm::Function &F, std::vector<int64_t> &ResultVector,
                        uint64_t Modulus, int VNumber, llvm::Type *IntType);

  bool runLLVMOptimizer();
};

} // namespace LSiMBA

#endif // LLVMPARSER_H