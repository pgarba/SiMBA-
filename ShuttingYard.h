#ifndef SHUTTINGYARD_H
#include <string>
#include <vector>

namespace llvm {
class Function;
class Type;
class Module;
}; // namespace llvm

int64_t eval(std::string expr, llvm::SmallVector<int64_t, 16> &par);

llvm::Function *createLLVMFunction(llvm::Module *M, llvm::Type *IntType,
                                   std::string &expr,
                                   std::vector<std::string> &VNames,
                                   uint64_t Modulus);

#endif