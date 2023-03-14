#include "Modulo.h"

#include <llvm/ADT/APInt.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/ErrorHandling.h>

/**
 * @brief Predefeined constants for the different bit widths.
 *
 */
const llvm::APInt Modulus_1(128, 2);

const llvm::APInt Modulus_2(128, 4);

const llvm::APInt Modulus_4(128, 16);

const llvm::APInt Modulus_8(128, 256);

const llvm::APInt Modules_16(128, 65536);

const llvm::APInt Modules_32(128, 4294967296);

// 18446744073709551616
const uint64_t Mod64[] = {0, 1};
const llvm::APInt Modules_64(128, 2, Mod64);

/**
 * @brief Get the Modulus object
 *
 * @param BitWidth
 * @return llvm::APInt&
 */
const llvm::APInt LSiMBA::getModulus(int BitWidth) {
  switch (BitWidth) {
  case 1:
    return Modulus_1;
  case 2:
    return Modulus_2;
  case 4:
    return Modulus_4;
  case 8:
    return Modulus_8;
  case 16:
    return Modules_16;
  case 32:
    return Modules_32;
  case 64:
    return Modules_64;
  default:
    llvm::report_fatal_error("Unsupported bit width: " +
                             std::to_string(BitWidth));
  }
}