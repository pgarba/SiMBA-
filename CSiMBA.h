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
 * The number of test cases to use for the verification.
 */
const int NUM_TEST_CASES = 16;

}  // namespace LSiMBA

#endif