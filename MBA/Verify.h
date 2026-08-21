// GAMBA native C++ port — result verification (fast-check + Z3 proof).
//
// fastCheckEquivalent: random-sampling equivalence check using the GAMBA
// evaluator (modular 2^bitCount semantics). Sound for every operator the
// GAMBA parser accepts; the default gate for non-native simplifier routes
// ("never trust an unverified result").
//
// proveEquivalent: Z3 proof that the two expressions are equivalent (uses
// the project's Z3 backend, Z3Prover.cpp). Note: the Z3 encoding of the
// power operator goes through FPA (see getZ3ExprFromString in
// ShuttingYard.cpp), so wide-bit powers are best-effort there; the
// fast-check remains the sound gate.
#pragma once

#include <string>

namespace LSiMBA {
namespace MBA {

// Compare orig and simp on numSamples random assignments (default 100,
// deterministic splitmix64 sequence). Prints a counterexample and returns
// false on the first mismatch; returns false if either expression fails
// to parse.
bool fastCheckEquivalent(const std::string &orig, const std::string &simp,
                         int bitCount, int numSamples = 100);

// Prove orig == simp with Z3 (bitCount-bit modular semantics). Returns
// true if proved, false otherwise (unknown, parse failure, or the Z3
// backend unavailable).
bool proveEquivalent(const std::string &orig, const std::string &simp,
                     int bitCount);

} // namespace MBA
} // namespace LSiMBA
