// MSiMBA semi-linear equivalence prover by signature lifting.
//
// The direct SAT refutation of "for all (x1..xt): E == GT" is intractable
// for the hard semi-linear class (measured: no CDCL solver — Kissat,
// CaDiCaL, Maplesat, CryptoMiniSat with native XOR — solves even the
// 16-bit instance of a line from data/MSiMBA/e1_4vars.txt in minutes;
// see plans/REMAINING_WORK_PLAN.md P1 gate result).
//
// Instead we use the MSiMBA signature theorem (arXiv:2406.10016): for
// expressions E, GT in the semi-linear class (per-bit affine in the
// variables with constants, plus sums of constant times per-bit-affine
// terms),
//
//   E(x1..xt) == GT(x1..xt)  for all (x1..xt)
//
// iff their multi-bit signature vectors are equal:
//
//   for every bit index i and every combination c in {0,1}^t:
//     E(c0 * 2^i, c1 * 2^i, ...) >> i  ==  GT(c0 * 2^i, ...) >> i
//
// i.e. N * 2^t concrete evaluations (256 at 32-bit with 3 vars, 512 at
// 64-bit). Each evaluation is a direct walk of the parsed bit-vector tree
// with concrete values (no SAT, no Z3 solving — Z3 is used only to parse,
// same as the QF_BV path). The theorem was empirically validated on
// 800 random MSiMBA-class pairs at 3-4 bits (signature equality ==
// exhaustive equivalence in every case; /tmp/theorem_check2.py).
//
// Safety: if either side is outside the semi-linear class (checked
// syntactically on the Z3 AST — the check is deliberately over-approximate
// in the ABSTAIN direction: anything not recognized abstains), the prover
// returns ABSTAIN and the caller falls back to the previous behavior.
// A differing signature point is a genuine counterexample candidate; the
// caller may still route through fast-check to exhibit it.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace LSiMBA {
namespace MBA {

// Semi-linear signature proving (requires the Z3 build: MBA_HAS_Z3).
//
// Returns:
//   1 = PROVED: all signature points equal -> E == GT for all inputs
//       (by the signature theorem; precondition class check passed).
//   0 = NOT PROVED: at least one signature point differs (*differingOut
//       receives the count, capped at 16).
//   2 = ABSTAIN: a side is outside the semi-linear class, parse failed,
//       or the Z3 backend is unavailable.
int proveSemiLinear(const std::string &e0, const std::string &e1,
                    int bitCount, const std::vector<std::string> &vars,
                    unsigned *differingOut = nullptr);

// Syntactic check: is `expr` in the MSiMBA semi-linear class (vars given
// as the variable list)? True = in class; false = not recognized (the
// check only makes the prover abstain when false; it must be sound in the
// sense "true implies the signature theorem applies to expr").
bool isSemiLinearClass(const std::string &expr, int bitCount,
                       const std::vector<std::string> &vars);

} // namespace MBA
} // namespace LSiMBA
