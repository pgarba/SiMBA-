#ifndef CSIMBA_H
#define CSIMBA_H

#include <cstdint>

#include <llvm/ADT/APInt.h>
#include <llvm/ADT/StringRef.h>

#include "Modulo.h"

namespace LSiMBA {

#define VERSION_MAJOR 2
#define VERSION_MINOR 0

/**
 * The number of *random* test cases to use for the fast (probabilistic)
 * equivalence check. Bumped from 16 to 256: the fast check is the cheap path,
 * so 256 evals of a small expression is still microseconds, and it turns the
 * filter from "catches obvious bugs" into "catches almost all real bugs".
 * (Structured corner samples are added on top of these in
 * Simplifier::probably_equivalent*.)
 */
const int NUM_TEST_CASES = 256;

}  // namespace LSiMBA

#endif