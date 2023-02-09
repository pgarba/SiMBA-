#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

using namespace llvm;

struct SiMBAPass : public FunctionPass {
  static char ID;
  SiMBAPass() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    errs() << "SiMBAPass: ";
    errs().write_escaped(F.getName()) << '\n';
    return false;
  }
}; 

char SiMBAPass::ID = 0;
static RegisterPass<SiMBAPass> X("simba", "SiMBAPass",
                             false /* Only looks at CFG */,
                             false /* Analysis Pass */);