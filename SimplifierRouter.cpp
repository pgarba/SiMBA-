// Phase 9: routing between the native SiMBA++ linear simplifier, the GAMBA
// native C++ port (general / nonlinear MBAs) and the vendored Python GAMBA.
// See SimplifierRouter.h for the option semantics.
#include "SimplifierRouter.h"

#include <cctype>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "llvm/Support/CommandLine.h"

#include "CSiMBA.h"
#include "MBA/GeneralSimplifier.h"
#include "MBA/LinearSimplifier.h"
#include "MBA/Parser.h"
#include "MBA/Verify.h"
#include "Simplifier.h"

// Existing options defined elsewhere (same category).
extern llvm::cl::OptionCategory SiMBAOpt;
extern llvm::cl::opt<int> MaxVarCount;    // LLVMParser.cpp
extern llvm::cl::opt<int> MinASTSize;     // LLVMParser.cpp
extern llvm::cl::opt<bool> ShouldWalkSubAST; // LLVMParser.cpp
extern llvm::cl::opt<int> timeout;        // Z3Prover.cpp (seconds, Phase 9)
extern llvm::cl::opt<std::string> PythonPath; // Simplifier.cpp
extern llvm::cl::opt<bool> EnableMod;      // Simplifier.cpp

// The --simplifier selection (Phase 9).
llvm::cl::opt<std::string> SimplifierChoice(
    "simplifier", llvm::cl::Optional,
    llvm::cl::desc("MBA simplifier to use: native | general | external | auto "
                  "(Default native)"),
    llvm::cl::value_desc("simplifier"), llvm::cl::init("native"),
    llvm::cl::cat(SiMBAOpt));

namespace LSiMBA {

namespace {

// --------------------------------------------------------------- utilities

std::string normalizeChoice(const std::string &in) {
  std::string out;
  for (char c : in)
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return out;
}

// Quote a single argument for CreateProcess (Windows does not use a shell).
std::string shellQuote(const std::string &s) {
  if (s.find(' ') == std::string::npos && s.find('"') == std::string::npos)
    return s;
  return "\"" + s + "\"";
}

// ---------------------------------------------------------- subprocess run

#ifdef _WIN32
#include <windows.h>

// Spawn `cmdLine`, merge stdout+stderr into `output`, enforce a wall-clock
// timeout in seconds. Returns 0 on normal exit (exitCode set), -1 on spawn
// failure (interpreter not found / bad path), -2 on timeout.
int runExternal(const std::string &cmdLine, int timeoutSec, std::string &output,
                int &exitCode) {
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.lpSecurityDescriptor = nullptr;
  sa.bInheritHandle = TRUE;

  HANDLE outRead = nullptr, outWrite = nullptr;
  if (!CreatePipe(&outRead, &outWrite, &sa, 0))
    return -1;

  STARTUPINFOA si;
  ZeroMemory(&si, sizeof(STARTUPINFOA));
  si.cb = sizeof(STARTUPINFOA);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = outWrite;
  si.hStdError = outWrite;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

  std::string cmd = cmdLine;
  BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, 0,
                          nullptr, nullptr, &si, &pi);
  if (!ok) {
    CloseHandle(outRead);
    CloseHandle(outWrite);
    return -1;
  }
  CloseHandle(outWrite);

  std::string out;
  char buf[8192];
  bool finished = false;
  int result = 0;

  int tsec = timeoutSec > 0 ? timeoutSec : 30;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(tsec);

  while (!finished) {
    DWORD status = WaitForSingleObject(outRead, 100);
    if (status == WAIT_OBJECT_0) {
      DWORD got = 0;
      if (ReadFile(outRead, buf, sizeof(buf), &got, nullptr) && got > 0) {
        out.append(buf, got);
      } else {
        finished = true; // EOF / read error
      }
      continue;
    }
    if (std::chrono::steady_clock::now() > deadline) {
      TerminateProcess(pi.hProcess, 1);
      DWORD got = 0;
      while (ReadFile(outRead, buf, sizeof(buf), &got, nullptr) && got > 0)
        out.append(buf, got);
      result = -2;
      break;
    }
  }

  WaitForSingleObject(pi.hProcess, 2000);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  exitCode = static_cast<int>(code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  CloseHandle(outRead);
  output = out;
  return result;
}

#else
#include <cstdio>
#include <sys/wait.h>

// Non-Windows fallback: popen (no in-process timeout; the Python GAMBA
// enforces its own internal timeout).
int runExternal(const std::string &cmdLine, int timeoutSec, std::string &output,
                int &exitCode) {
  (void)timeoutSec;
  FILE *p = popen(cmdLine.c_str(), "r");
  if (!p)
    return -1;
  char buf[8192];
  std::string out;
  while (fgets(buf, sizeof(buf), p) != nullptr)
    out.append(buf);
  int status = pclose(p);
  exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
  output = out;
  return 0;
}
#endif

// ------------------------------------------------- Python interpreter probe

// The vendored GAMBA scripts require numpy; probe the candidate interpreters
// and cache the first one that can import numpy. Returns "" if none works.
const std::string &findPythonWithNumpy() {
  static std::string cached = "UNPROBED";
  if (cached != "UNPROBED")
    return cached;

  std::vector<std::string> candidates;
  std::string pp = PythonPath.getValue();
  if (!pp.empty())
    candidates.push_back(pp);
  candidates.push_back("python");
  candidates.push_back("py");
  // Known interpreter with numpy installed in this environment (last resort).
  candidates.push_back("C:\\Python\\Python312\\python.exe");

  std::string found = "";
  for (const auto &c : candidates) {
    std::string out;
    int code = 0;
    int r = runExternal(shellQuote(c) + " -c \"import numpy\"", 15, out, code);
    if (r == 0 && code == 0) {
      found = c;
      break;
    }
  }
  cached = found.empty() ? std::string("NONE") : found;
  return cached;
}

// ------------------------------------------------------------- expression info

int getVarCount(const std::string &expr) {
  std::string e = expr;
  return static_cast<int>(LSiMBA::Simplifier::getVariables(e).size());
}

// AST size = node count of the GAMBA parse tree; on parse failure fall back
// to the string length as a proxy.
int getAstSize(const std::string &expr, int bitCount) {
  auto root = LSiMBA::MBA::parse(expr, bitCount, true, false, false);
  if (root != nullptr)
    return root->countNodes();
  return static_cast<int>(expr.size());
}

// ------------------------------------------------------- external GAMBA call

// Invoke the vendored Python GAMBA on the expression.
//   linear   -> external/GAMBA/src/simplify.py
//   nonlinear-> external/GAMBA/src/simplify_general.py
// Returns true if a result was produced (res set). Prints a warning and sets
// fallbackNative when the script or a suitable Python is unavailable.
bool runExternalGamba(const std::string &expr, int bitCount, bool useZ3,
                      std::string &res, bool &fallbackNative) {
  fallbackNative = false;

  bool linear = LSiMBA::MBA::checkLinear(expr, bitCount);
  std::string script =
      linear ? "external/GAMBA/src/simplify.py"
             : "external/GAMBA/src/simplify_general.py";

  if (!std::filesystem::exists(script)) {
    printf("[!] External simplifier script not found: %s - falling back to "
           "native\n",
           script.c_str());
    fallbackNative = true;
    return false;
  }

  const std::string &python = findPythonWithNumpy();
  if (python == "NONE") {
    printf("[!] No Python interpreter with numpy available - falling back to "
           "native\n");
    fallbackNative = true;
    return false;
  }

  std::string cmd = shellQuote(python) + " " + shellQuote(script) + " " +
                    shellQuote(expr) + " -b " + std::to_string(bitCount);
  if (useZ3)
    cmd += " -z";
  if (EnableMod)
    cmd += " -m";

  std::string out;
  int code = 0;
  int r = runExternal(cmd, timeout, out, code);

  if (r == -1) {
    printf("[!] Could not start Python (%s) - falling back to native\n",
           python.c_str());
    fallbackNative = true;
    return false;
  }

  if (r == -2) {
    printf("[!] External simplifier timed out after %ds\n", timeout.getValue());
    return false;
  }

  // Parse the "*** ... simplified to <simpl>" marker line.
  const std::string Marker = "*** ... simplified to ";
  std::string line;
  std::istringstream ss(out);
  while (std::getline(ss, line)) {
    if (line.compare(0, Marker.size(), Marker) == 0) {
      std::string s = line.substr(Marker.size());
      // Strip trailing whitespace/newlines.
      while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
      if (!s.empty()) {
        res = s;
        return true;
      }
    }
  }
  return false;
}

// ------------------------------------------------- walk-sub-ast fallback

std::vector<std::string> splitTopLevelTerms(const std::string &expr) {
  std::vector<std::string> terms;
  int depth = 0;
  std::string cur;
  for (char c : expr) {
    if (c == '(')
      depth++;
    else if (c == ')')
      depth--;
    if (c == '+' && depth == 0) {
      terms.push_back(cur);
      cur.clear();
      continue;
    }
    cur.push_back(c);
  }
  terms.push_back(cur);
  return terms;
}

// Simplify one top-level term with the selected (non-native) simplifier.
bool simplifyOneTerm(const std::string &term, std::string &res, int bitCount,
                     bool useZ3, const std::string &choice) {
  if (choice == "general") {
    int tsec = timeout > 0 ? timeout : 25;
    res = LSiMBA::MBA::simplifyMba(term, bitCount, useZ3, false, -1, tsec);
    return !res.empty();
  }
  bool fallback = false;
  return runExternalGamba(term, bitCount, useZ3, res, fallback);
}

// Fallback: when the full expression cannot be simplified, simplify each
// top-level term (split at top-level '+') separately and recombine. Each term
// is simplified to an equivalent expression, so the recombined sum is
// equivalent as well.
std::string walkTopLevelTerms(const std::string &expr, int bitCount, bool useZ3,
                              const std::string &choice) {
  auto terms = splitTopLevelTerms(expr);
  if (terms.size() < 2)
    return "";

  std::vector<std::string> parts;
  int simplified = 0;
  for (const auto &t : terms) {
    std::string term = LSiMBA::Simplifier::strip(t);
    if (term.empty())
      continue;
    std::string s = "";
    if (simplifyOneTerm(term, s, bitCount, useZ3, choice)) {
      parts.push_back(s);
      simplified++;
    } else {
      parts.push_back(term); // keep the original term
    }
  }
  if (simplified == 0)
    return "";

  std::string res;
  for (size_t i = 0; i < parts.size(); i++) {
    if (i)
      res += "+";
    res += parts[i];
  }
  return res;
}

// ------------------------------------------------------------- core routing

// Returns the effective (non-native) choice for the given expression, or ""
// if the caller should keep its original native path.
std::string effectiveChoice(const std::string &expr, int bitCount) {
  std::string choice = normalizeChoice(SimplifierChoice.getValue());

  if (choice == "auto") {
    if (LSiMBA::MBA::checkLinear(expr, bitCount))
      return ""; // linear: keep the existing native path
    choice = "general";
  }

  if (choice == "native" || choice.empty())
    return "";
  if (choice != "general" && choice != "external") {
    printf("[!] Unknown --simplifier value '%s' - falling back to native\n",
           SimplifierChoice.getValue().c_str());
    return "";
  }
  return choice;
}

// Applies the --max-var-count / --min-ast-size gates. Returns true if the
// expression should be skipped (and prints the reason).
bool isGatedOut(const std::string &expr, int bitCount) {
  if (MaxVarCount > 0) {
    int vcount = getVarCount(expr);
    if (vcount > MaxVarCount) {
      printf("[!] Skipped: %d variables (max-var-count %d)\n", vcount,
             MaxVarCount.getValue());
      return true;
    }
  }
  if (MinASTSize > 0) {
    int astSize = getAstSize(expr, bitCount);
    if (astSize < MinASTSize) {
      printf("[!] Skipped: AST size %d (min-ast-size %d)\n", astSize,
             MinASTSize.getValue());
      return true;
    }
  }
  return false;
}

} // namespace

// Verify a non-native result before it is reported as a valid replacement
// (AC2: never trust an unverified result). The fast-check is a random-value
// equivalence check with the GAMBA evaluator (modular 2^bitCount semantics);
// it runs when --fastcheck is on (default). Returns true if the result may
// be reported as SUCCESS, false if it must be reported as INVALID.
bool verifyNonNativeResult(const std::string &orig, const std::string &res,
                           int bitCount, bool fastCheck) {
  if (fastCheck && !LSiMBA::MBA::fastCheckEquivalent(orig, res, bitCount))
    return false;
  return true;
}

RouteResult RouteSimplify(const std::string &MBA, std::string &SimpMBA,
                          int bitCount, bool useZ3, bool fastCheck,
                          bool runParallel, bool checkLinear) {
  (void)runParallel;
  (void)checkLinear; // the native path applies these itself

  std::string choice = effectiveChoice(MBA, bitCount);
  if (choice.empty())
    return RouteResult::NATIVE;

  if (isGatedOut(MBA, bitCount))
    return RouteResult::SKIPPED;

  if (choice == "general") {
    int tsec = timeout > 0 ? timeout : 25;
    std::string res =
        LSiMBA::MBA::simplifyMba(MBA, bitCount, useZ3, false, -1, tsec);
    if (res.empty() && ShouldWalkSubAST)
      res = walkTopLevelTerms(MBA, bitCount, useZ3, "general");
    if (res.empty())
      return RouteResult::FAILED;
    SimpMBA = res;
    if (!verifyNonNativeResult(MBA, res, bitCount, fastCheck))
      return RouteResult::INVALID;
    return RouteResult::SUCCESS;
  }

  // external
  bool fallback = false;
  if (runExternalGamba(MBA, bitCount, useZ3, SimpMBA, fallback)) {
    if (!verifyNonNativeResult(MBA, SimpMBA, bitCount, fastCheck))
      return RouteResult::INVALID;
    return RouteResult::SUCCESS;
  }
  if (fallback)
    return RouteResult::NATIVE; // Python/script unavailable: use the native path

  if (ShouldWalkSubAST) {
    std::string res = walkTopLevelTerms(MBA, bitCount, useZ3, "external");
    if (!res.empty()) {
      SimpMBA = res;
      if (!verifyNonNativeResult(MBA, res, bitCount, fastCheck))
        return RouteResult::INVALID;
      return RouteResult::SUCCESS;
    }
  }
  return RouteResult::FAILED;
}

bool TrySelectedSimplifier(const std::string &Expr, std::string &SimpMBA,
                           int bitWidth, bool useZ3) {
  std::string choice = effectiveChoice(Expr, bitWidth);
  if (choice.empty())
    return false;

  if (isGatedOut(Expr, bitWidth))
    return false;

  if (choice == "general") {
    int tsec = timeout > 0 ? timeout : 25;
    std::string res =
        LSiMBA::MBA::simplifyMba(Expr, bitWidth, useZ3, false, -1, tsec);
    if (res.empty() && ShouldWalkSubAST)
      res = walkTopLevelTerms(Expr, bitWidth, useZ3, "general");
    if (res.empty())
      return false;
    SimpMBA = res;
    return true;
  }

  bool fallback = false;
  if (runExternalGamba(Expr, bitWidth, useZ3, SimpMBA, fallback))
    return true;
  if (fallback)
    return false; // use the original native path

  if (ShouldWalkSubAST) {
    std::string res = walkTopLevelTerms(Expr, bitWidth, useZ3, "external");
    if (!res.empty()) {
      SimpMBA = res;
      return true;
    }
  }
  return false;
}

} // namespace LSiMBA
