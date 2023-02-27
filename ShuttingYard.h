#ifndef SHUTTINGYARD_H
#include <string>
#include <vector>

namespace llvm {
class APInt;
class Function;
class Instruction;
class Module;
class Type;
class Value;
}; // namespace llvm

llvm::APInt eval(std::string expr, llvm::SmallVector<llvm::APInt, 16> &par,
                 int BitWidth,
                 int *OperationCount = nullptr);

llvm::Function *createLLVMFunction(llvm::Module *M, llvm::Type *IntType,
                                   std::string &expr,
                                   std::vector<std::string> &VNames);

void createLLVMReplacement(llvm::Instruction *InsertionPoint,
                           llvm::Type *IntType, std::string &expr,
                           std::vector<std::string> &VNames,
                           llvm::SmallVectorImpl<llvm::Value *> &Variables);

#endif