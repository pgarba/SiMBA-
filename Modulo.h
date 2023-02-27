#ifndef MODULO_H
#define MODULO_H

#include <cstdint>

namespace llvm {
class APInt;
}

namespace LSiMBA {

/**
 * @brief Get the Modulus object
 *
 * @param BitWidth
 * @return llvm::APInt&
 */
const llvm::APInt getModulus(int BitWidth);

} // namespace LSiMBA

#endif
