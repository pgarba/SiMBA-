#include "SiMBAPass.h"

#include <chrono>

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

#include "LLVMParser.h"

using namespace std;
using namespace llvm;
using namespace std::chrono;

cl::opt<bool> UseSigned("signed", cl::Optional,
                        cl::desc("Evaluate as signed values (Default true)"),
                        cl::value_desc("signed"), cl::init(true));

cl::opt<bool>
    UseFastCheck("fastcheck", cl::Optional,
                 cl::desc("Verify MBA with random values (Default true)"),
                 cl::value_desc("fastcheck"), cl::init(true));

cl::opt<bool> RunOptimizer(
    "optimize", cl::Optional,
    cl::desc("Optimize LLVM IR before simplification (Default true)"),
    cl::value_desc("optimize"), cl::init(true));

bool SiMBAPass::runOnModule(Module &M) {
  outs() << "[+] Loading LLVM Module: '" << M.getName() << "'\n";

  LSiMBA::LLVMParser Parser(&M, 64, UseSigned, false, UseFastCheck, false);

  auto start = high_resolution_clock::now();

  int MBACount = Parser.simplify();

  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(stop - start);

  outs() << "[+] MBAs found and replaced: '" << MBACount
         << "' time: " << (int)duration.count() << "ms\n";

  return MBACount > 0;
}

bool SiMBAPass::runOnModuleStatic(Module &M) {
  outs() << "[+] Loading LLVM Module: '" << M.getName() << "'\n";

  LSiMBA::LLVMParser Parser(&M, 64, UseSigned, false, UseFastCheck, false, true,
                            true);

  auto start = high_resolution_clock::now();

  int MBACount = Parser.simplify();

  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(stop - start);

  outs() << "[+] MBAs found and replaced: '" << MBACount
         << "' time: " << (int)duration.count() << "ms\n";

  return MBACount > 0;
}

char SiMBAPass::ID = 0;

static RegisterPass<SiMBAPass> X("simba", "SiMBA++ MBA simplification pass",
                                 false /* Only looks at CFG */,
                                 false /* Analysis Pass */);

// INITIALIZE_PASS(SiMBA, "simba", "SiMBA++ MBA simplification pass", false,
// false)

ModulePass *llvm::createSiMBAPass() { return new SiMBAPass(); }