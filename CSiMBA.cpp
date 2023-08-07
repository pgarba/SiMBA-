#include "CSiMBA.h"

#include <chrono>
#include <filesystem>
#include <fstream>

#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/InitLLVM.h"

#include "CSiMBA.h"
#include "LLVMParser.h"
#include "MBAChecker.h"
#include "ShuttingYard.h"
#include "Simplifier.h"

using namespace std;
using namespace llvm;
using namespace std::chrono;

/*
  Options
*/
cl::opt<std::string> StrMBA("mba", cl::Optional,
                            cl::desc("MBA that will be verfied/simplified"),
                            cl::value_desc("mba"), cl::init(""));

cl::opt<std::string>
    StrMBADB("mbadb", cl::Optional,
             cl::desc("MBA database that will be verfied/simplified"),
             cl::value_desc("mbadb"), cl::init(""));

cl::opt<std::string> StrIR("ir", cl::Optional,
                           cl::desc("LLVM Module that contains MBA functions "
                                    "that will be verfied/simplified"),
                           cl::value_desc("ir"), cl::init(""));

cl::opt<std::string>
    ConvertToLLVM("convert-to-llvm", cl::Optional,
                  cl::desc("Converts the MBA database to LLVM"),
                  cl::value_desc("Database output name"), cl::init(""));

cl::opt<std::string> Output("out", cl::Optional,
                            cl::desc("Stores the output into this file"),
                            cl::value_desc("out"), cl::init(""));

cl::opt<int> StopN("stop", cl::Optional,
                   cl::desc("Stop after N MBAs are solved (Default 0)"),
                   cl::value_desc("stop"), cl::init(0));

cl::opt<int> BitCount("bitcount", cl::Optional,
                      cl::desc("Bitcount of the variables (Default 64)"),
                      cl::value_desc("BitCount"), cl::init(64));

cl::opt<bool> DetectAndSimplify(
    "detect-simplify", cl::Optional,
    cl::desc(
        "Search for MBAs in LLVM Module and try to simplify (Default false)"),
    cl::value_desc("detect-simplify"), cl::init(false));

cl::opt<bool>
    UseFastCheck("fastcheck", cl::Optional,
                 cl::desc("Verify MBA with random values (Default true)"),
                 cl::value_desc("fastcheck"), cl::init(true));

cl::opt<bool>
    IgnoreExpected("ignore-expected", cl::Optional,
                   cl::desc("Ignores the expected string (Default false)"),
                   cl::value_desc("ignore-expected"), cl::init(false));

cl::opt<bool>
    CheckLinear("checklinear", cl::Optional,
                cl::desc("Check if MBA is a linear expresssion (Default true)"),
                cl::value_desc("checklinear"), cl::init(true));

cl::opt<bool> SimplifyExpected(
    "simplify-expected", cl::Optional,
    cl::desc("Simplify the expected value to match it (Default false)"),
    cl::value_desc("simplify-expected"), cl::init(false));

cl::opt<bool>
    RunParallel("parallel", cl::Optional,
                cl::desc("Evaluate/Check MBA expressions in parallel, give a "
                         "nice boost on MBA with > 3 vars (Default true)"),
                cl::value_desc("parallel"), cl::init(true));

cl::opt<bool>
    ProveZ3("prove", cl::Optional,
            cl::desc("Prove with Z3 that the MBA is correct (Default false)"),
            cl::value_desc("prove"), cl::init(false));

cl::opt<bool> RunOptimizer(
    "optimize", cl::Optional,
    cl::desc("Optimize LLVM IR before simplification (Default true)"),
    cl::value_desc("optimize"), cl::init(true));

cl::opt<bool> Debug("simba-debug", cl::Optional,
                    cl::desc("Print debug information (Default false)"),
                    cl::value_desc("simba-debug"), cl::init(false));

/**
 * @brief Simplify a single MBA
 */
void SimplifySingleMBA();

/**
 * @brief Simplify a MBA database
 */
void SimplifyMBADatabase();

/**
 * @brief Simplify a LLVM module
 */
void SimplifyLLVMModule();

/**
 * @brief Simplify a LLVM module with  MBA functions
 */
void SimplifyLLVMDataBase();

/**
 * @brief Run Simplifier
 *
 * @param MBA The MBA to simplify
 * @param SimpMBA The simplified MBA
 * @param ExpMBA The expected MBA
 * @param Counter The current counter
 * @param Valid The valid counter
 */
void RunSimplifier(std::string &MBA, std::string &SimpMBA, std::string &ExpMBA,
                   int &Counter, int &Valid);

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  llvm::cl::ParseCommandLineOptions(argc, argv);

  // Print a SiMBA logo
  printf("   _____ __  ______  ___    __    __ \n");
  printf("  / __(_)  |/  / _ )/ _ |__/ /___/ /_\n");
  printf(" _\\ \\/ / /|_/ / _  / __ /_  __/_  __/\n");
  printf("/___/_/_/  /_/____/_/ |_|/_/   /_/%d.%d\n", VERSION_MAJOR,
         VERSION_MINOR);
  printf("°°SiMBA ported to C/C++/LLVM ~pgarba~\n\n");

  if (StrMBA == "" && StrMBADB == "" && StrIR == "") {
    llvm::cl::PrintHelpMessage();
    return 0;
  }

  if (StrIR != "" && !DetectAndSimplify) {
    SimplifyLLVMDataBase();
  } else if (StrIR != "" && DetectAndSimplify) {
    SimplifyLLVMModule();
  } else if (StrMBADB != "") {
    SimplifyMBADatabase();
  } else {
    SimplifySingleMBA();
  }

  return 0;
}

void SimplifySingleMBA() {
  std::string SimpMBA = "";
  auto start = high_resolution_clock::now();
  auto Result = LSiMBA::Simplifier::simplify_linear_mba(
      StrMBA, SimpMBA, BitCount, ProveZ3, CheckLinear);
  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(stop - start);

  if (Result == true) {
    printf("[+] [Simplified MBA] '%s' time: %dms\n", SimpMBA.c_str(),
           (int)duration.count());
  } else {
    printf("[!] [Simplified MBA] Not valid replacement! '%s' time: %dms\n",
           SimpMBA.c_str(), (int)duration.count());
  }
}

void ConvertToLLVMFunction(llvm::Module *M, std::string &StrMBA) {
  auto IntTy =
      llvm::Type::getIntNTy(LSiMBA::LLVMParser::getLLVMContext(), BitCount);
  auto Vars = LSiMBA::Simplifier::getVariables(StrMBA);

  // Create LLVM Function
  auto F = createLLVMFunction(M, IntTy, StrMBA, Vars);

  // Set name
  F->setName("MBA");
}

void SimplifyMBADatabase() {
  // read lines from file
  std::ifstream infile(StrMBADB);
  if (infile.is_open() == false) {
    outs() << "[!] Could not open database: '" << StrMBADB << "'\n";
    return;
  }

  std::string line;
  int All = 0;
  int Counter = 0;
  int Valid = 0;

  unique_ptr<llvm::Module> LLVMModule;
  if (ConvertToLLVM != "") {
    LLVMModule = make_unique<llvm::Module>(
        StrMBADB, LSiMBA::LLVMParser::getLLVMContext());
  }

  auto start = high_resolution_clock::now();
  while (std::getline(infile, line)) {
    // Skip comments
    if (line[0] == '#')
      continue;

    // split line at '#' and strip whitespaces
    auto MBA = line.substr(0, line.find(','));
    MBA = LSiMBA::Simplifier::strip(MBA);

    // get line after ','
    auto ExpMBA = line.substr(line.find(',') + 1, line.size());
    ExpMBA = LSiMBA::Simplifier::strip(ExpMBA);

    // Remove whitespaces
    ExpMBA.erase(std::remove_if(ExpMBA.begin(), ExpMBA.end(), ::isspace),
                 ExpMBA.end());

    All++;
    Counter++;

    if (ConvertToLLVM != "") {
      // Just convert to LLVM dont simplify
      ConvertToLLVMFunction(LLVMModule.get(), MBA);
      Valid++;
    } else {
      // Run Simplifier
      std::string SimpMBA = "";

      // Simplify MBA
      RunSimplifier(MBA, SimpMBA, ExpMBA, Counter, Valid);
    }
  }

  // print stats
  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(stop - start);
  printf("[+] MBAs: %d -> Counter: %d Valid: %d time: %dms avg: %lfms\n", All,
         Counter, Valid, (int)duration.count(),
         (double)duration.count() / (double)All);

  // Write LLVM Module
  if (ConvertToLLVM != "") {
    std::error_code EC;
    llvm::raw_fd_ostream OS(ConvertToLLVM, EC, llvm::sys::fs::OF_None);
    if (EC) {
      outs() << "[!] Could not open file: '" << ConvertToLLVM << "'\n";
      return;
    }

    OS << *LLVMModule;
    OS.close();

    outs() << "[+] Wrote LLVM Module to: '" << ConvertToLLVM << "'\n";
  }
}

void SimplifyLLVMDataBase() {
  outs() << "[+] Loading LLVM MBA Module: '" << StrIR << "'\n";

  LSiMBA::LLVMParser Parser(StrIR, Output, RunParallel, UseFastCheck,
                            RunOptimizer, RunOptimizer, Debug, ProveZ3);

  auto start = high_resolution_clock::now();

  int MBACount = Parser.simplifyMBAFunctionsOnly();

  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(stop - start);

  outs() << "[+] MBAs: '" << MBACount << "' time: " << (int)duration.count()
         << "ms\n";
}

void SimplifyLLVMModule() {
  outs() << "[+] Loading LLVM Module: '" << StrIR << "'\n";

  LSiMBA::LLVMParser Parser(StrIR, Output, BitCount, RunParallel, UseFastCheck,
                            RunOptimizer, Debug, ProveZ3);

  auto start = high_resolution_clock::now();

  int MBACount = Parser.simplify();

  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<milliseconds>(stop - start);

  outs() << "[+] MBAs found and replaced: '" << MBACount
         << "' time: " << (int)duration.count() << "ms\n";

  outs() << "[+] : '" << Parser.getInstructionCountBefore()
         << "' after: " << Parser.getInstructionCountAfter() << " (-"
         << int(Parser.getInstructionCountAfter() * 100. /
                Parser.getInstructionCountBefore())
         << "%)\n";
}

void RunSimplifier(std::string &MBA, std::string &SimpMBA, std::string &ExpMBA,
                   int &Counter, int &Valid) {
  // Simplify MBA
  auto Result = LSiMBA::Simplifier::simplify_linear_mba(
      MBA, SimpMBA, BitCount, ProveZ3, CheckLinear, UseFastCheck, RunParallel);

  // Simplify Groundtruth
  if (IgnoreExpected == false && SimplifyExpected == true) {
    LSiMBA::Simplifier::simplify_linear_mba(MBA, ExpMBA, BitCount, false, false,
                                            false, RunParallel);
  }

  // Check if valid replacement
  if (Result == true) {
    Valid++;
    // printf("[%d] Valid Transformation!\n", Counter);
    if (!IgnoreExpected && (ExpMBA != SimpMBA)) {
      printf("[%d] Replacement is valid but does not meet expected string: "
             "(%s != %s)\n",
             Counter, ExpMBA.c_str(), SimpMBA.c_str());
    }
  } else {
    printf("[%d] Not Valid Transformation! (%s != %s) <=> %s\n", Counter,
           ExpMBA.c_str(), SimpMBA.c_str(), MBA.c_str());
  }
}
