#include "llvm/IR/Function.h"
#include "llvm/Pass.h"

using namespace llvm;

namespace llvm {

class SiMBAPass : public ModulePass {
public:
  static char ID;
  SiMBAPass() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;

  static bool runOnModuleStatic(Module &M);
}; // end of struct SiMBAPass

ModulePass *createSiMBAPass();

} // namespace llvm