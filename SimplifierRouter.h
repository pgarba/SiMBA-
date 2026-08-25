// Phase 9: routing between the native SiMBA++ linear simplifier, the GAMBA
// native C++ port (general / nonlinear MBAs, namespace LSiMBA::MBA), and the
// vendored Python GAMBA (external/GAMBA).
//
// The --simplifier option selects the route (default: auto):
//   native   - existing LSiMBA::Simplifier::simplify_linear_mba (the call
//              sites keep their original code path untouched)
//   general  - LSiMBA::MBA::simplifyMba (nonlinear MBAs)
//   external - vendored Python GAMBA as a subprocess (simplify.py for linear,
//              simplify_general.py for nonlinear); falls back to native with a
//              warning if Python is unavailable
//   msimba   - LSiMBA::MBA::MultibitSimplifier (semi-linear MBAs: constants
//              inside bitwise operands); polynomial, works at 64-bit
//   auto     - (default) LSiMBA::MBA::checkLinear: linear -> native;
//              semi-linear (MultibitSimplifier::isSemiLinear) -> msimba;
//              nonlinear -> general
//
// The --max-var-count / --min-ast-size gates and the --walk-sub-ast fallback
// apply to the general/external routes only; the native route (and the linear
// half of auto) is left completely unchanged so the baseline is preserved.
//
// --timeout (seconds) bounds the GeneralSimplifier wall-clock deadline and the
// external Python subprocess; --print-smt and --accept-unknown keep their
// existing Z3-proving semantics (see Z3Prover.cpp).
#ifndef SIMPLIFIER_ROUTER_H
#define SIMPLIFIER_ROUTER_H

#include <string>

namespace LSiMBA {

// Outcome of routing an MBA string to the --simplifier selection.
enum class RouteResult {
  NATIVE,   // selection is native (or auto-detected linear): the caller uses
            // the existing LSiMBA::Simplifier::simplify_linear_mba path.
  SUCCESS,  // the selected simplifier produced a result (SimpMBA set).
  SKIPPED,  // gated out by --max-var-count / --min-ast-size.
  FAILED,   // the selected simplifier ran but produced no result.
  INVALID,  // a result was produced (SimpMBA set) but it failed verification
            // (fast-check counterexample or Z3): never reported as a valid
            // replacement.
};

// Route the MBA string to the --simplifier selection.
// Honors --max-var-count / --min-ast-size gates and the --walk-sub-ast
// top-level-term fallback. See the header comment for the semantics.
//
// autoFallback (library option; CLI: --auto-fallback, default true): when the
// selection is `auto` and the classified route produces no result, try the
// remaining non-native routes before reporting FAILED. Ignored for an
// explicit --simplifier=X (single route, no fallback).
RouteResult RouteSimplify(const std::string &MBA, std::string &SimpMBA,
                          int bitCount, bool useZ3, bool fastCheck,
                          bool runParallel, bool checkLinear,
                          bool autoFallback = true);

// LLVM paths: try the selected simplifier on a string expression extracted
// from an AST (getASTAsString). Returns true if a replacement was produced
// (SimpMBA set); the caller's existing verify() step still validates it.
// Returns false for the native selection so the caller keeps its original
// path, and false when the selected simplifier produced no result (the
// caller then falls back to its original path as well).
// autoFallback: as for RouteSimplify (try the other routes in auto mode).
bool TrySelectedSimplifier(const std::string &Expr, std::string &SimpMBA,
                           int bitWidth, bool useZ3, bool autoFallback = true);

// Try the non-native routes (msimba, general, external) in that order on the
// expression, skipping the route named by `skip`. Each candidate is
// fast-checked (when fastCheck is true) and skipped if it fails, so the first
// result returned is one that passed verification. Returns true if a result
// was produced (SimpMBA set).
// This is the auto-fallback engine; it is also public so library callers who
// run the native path themselves can fall back to the other routes when
// native produces nothing.
bool TryAutoFallback(const std::string &MBA, std::string &SimpMBA,
                     int bitCount, bool useZ3, bool fastCheck,
                     const std::string &skip = "");

// True when the auto-fallback is active: --simplifier=auto AND --auto-fallback.
bool autoFallbackActive();

// The --auto-fallback flag value (library accessor so callers need not link
// against the llvm::cl::opt directly). Pass this as the autoFallback argument
// of RouteSimplify / TrySelectedSimplifier.
bool autoFallbackEnabled();

} // namespace LSiMBA

#endif // SIMPLIFIER_ROUTER_H
