#ifndef SHUTTINGYARD_H
#include <map>
#include <string>
#include <vector>

#include <llvm/ADT/SmallVector.h>

#include <z3++.h>

namespace llvm {
class APInt;
class Function;
class Instruction;
class Module;
class Type;
class Value;
}; // namespace llvm

/*
    Get the count of operators
*/
int countOperators(std::string &expr);

llvm::APInt eval(std::string expr, llvm::SmallVectorImpl<llvm::APInt> &par,
                 int BitWidth, int *OperationCount = nullptr);

llvm::Function *createLLVMFunction(llvm::Module *M, llvm::Type *IntType,
                                   std::string &expr,
                                   std::vector<std::string> &VNames);

void createLLVMReplacement(llvm::Instruction *InsertionPoint,
                           llvm::Type *IntType, std::string &expr,
                           std::vector<std::string> &VNames,
                           llvm::SmallVectorImpl<llvm::Value *> &Variables);

z3::expr getZ3ExprFromString(z3::context &Z3Ctx, std::string &expr,
                             int BitWidth, std::vector<std::string> &Variables,
                             std::map<std::string, llvm::Type *> &VarTypes,
                             std::map<std::string, z3::expr *> &VarMap);

#endif