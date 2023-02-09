#ifndef SHUTTINGYARD_H
#include <string>
#include <vector>

namespace llvm {
class Function;
class Instruction;
class Module;
class Type;
class Value;
}; // namespace llvm

int64_t eval(std::string expr, llvm::SmallVector<int64_t, 16> &par,
             int *OperationCount = nullptr);

llvm::Function *createLLVMFunction(llvm::Module *M, llvm::Type *IntType,
                                   std::string &expr,
                                   std::vector<std::string> &VNames,
                                   uint64_t Modulus);

void createLLVMReplacement(llvm::Instruction *InsertionPoint,
                           llvm::Type *IntType, std::string &expr,
                           std::vector<std::string> &VNames,
                           llvm::SmallVectorImpl<llvm::Value *> &Variables);

#endif