#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"

#include <chrono>
#include <filesystem>
#include <fstream>

#include "MBAChecker.h"
#include "Simplifier.h"

#include "ShuttingYard.h"

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

cl::opt<int> StopN("stop", cl::Optional,
                   cl::desc("Stop after N MBAs (Default 0)"),
                   cl::value_desc("stop"), cl::init(0));

cl::opt<int> BitCount("bitcount", cl::Optional,
                      cl::desc("Bitcount of the variables (Default 64)"),
                      cl::value_desc("BitCount"), cl::init(64));

cl::opt<bool> UseZ3("z3", cl::Optional,
                    cl::desc("Verify MBA with Z3 (Default false)"),
                    cl::value_desc("UseZ3"), cl::init(false));

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
                cl::desc("Check if MBA is a linear expresssion (default true)"),
                cl::value_desc("checklinear"), cl::init(true));

cl::opt<bool> UseSigned("signed", cl::Optional,
                        cl::desc("Evaluate as signed values (default true)"),
                        cl::value_desc("signed"), cl::init(true));

cl::opt<bool> RunParallel("parallel", cl::Optional,
                        cl::desc("Evaluate/Check MBA expressions in parallel, give a nice boost on MBA with > 3 vars (default true)"),
                        cl::value_desc("parallel"), cl::init(true));

/*
 * This is the main function of the program. It is called when the program is
 * started. It is the entry point of the program.
 */
int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  llvm::cl::ParseCommandLineOptions(argc, argv);

  printf("CSiMBA - SiMBA in C++\n\n");

  if (StrMBA == "" && StrMBADB == "") {
    llvm::cl::PrintHelpMessage();
    return 0;
  }

  // LoadDB and parse MBAs
  if (StrMBADB != "") {
    // read lines from file
    std::ifstream infile(StrMBADB);
    if (infile.is_open() == false) {
      outs() << "[!] Could not open database: '" << StrMBADB << "'\n";
      return 1;
    }

    std::string line;
    int All = 0;
    int Counter = 0;
    int Valid = 0;

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

      // Run Simplifier
      std::string SimpMBA = "";
      All++;
      Counter++;

      auto Result = LSiMBA::Simplifier::simplify_linear_mba(
          UseSigned, MBA, SimpMBA, BitCount, UseZ3, CheckLinear, UseFastCheck, RunParallel);

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

      if (All == StopN) {
        break;
      }
    }

    // print stats
    auto stop = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(stop - start);
    printf("MBAs: %d -> Counter: %d Valid: %d time: %dms avg: %lfms\n", All,
           Counter, Valid, (int) duration.count(),
           (double)duration.count() / (double)All);
  } else {
    // Run Simplifier
    std::string SimpMBA = "";
    auto start = high_resolution_clock::now();
    auto Result = LSiMBA::Simplifier::simplify_linear_mba(
        UseSigned, StrMBA, SimpMBA, BitCount, UseZ3, CheckLinear);
    auto stop = high_resolution_clock::now();
    auto duration = duration_cast<milliseconds>(stop - start);

    if (Result == true) {
      printf("[Simplified MBA] '%s time: %dms'\n", SimpMBA.c_str(),
             (int) duration.count());
    } else {
      printf("[Simplified MBA] Not valid replacement! '%s time: %dms'\n",
             SimpMBA.c_str(), (int) duration.count());
    }
  }

  return 0;
}
