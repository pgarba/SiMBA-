// Options shared by the SiMBA++ front ends and the SiMBACore library. They
// lived in LLVMParser.cpp; the core (SimplifierRouter) reads them, so they
// are defined here to keep the core free of the LLVM-IR front end.
#include "llvm/Support/CommandLine.h"

using namespace llvm;

cl::OptionCategory SiMBAOpt("SiMBA++ Options");

llvm::cl::opt<int> MaxVarCount(
    "max-var-count", cl::Optional,
    cl::desc("Max variable count for simplification"),
    cl::value_desc("max-var-count"), cl::init(10), cl::cat(SiMBAOpt));

llvm::cl::opt<int> MinASTSize("min-ast-size", cl::Optional,
                              cl::desc("Minimum AST size for simplification"),
                              cl::value_desc("min-ast-size"), cl::init(3),
                              cl::cat(SiMBAOpt));

llvm::cl::opt<bool> ShouldWalkSubAST(
    "walk-sub-ast", cl::Optional,
    cl::desc("Walk sub AST if full AST does not match"),
    cl::value_desc("walk-sub-ast"), cl::init(false), cl::cat(SiMBAOpt));
