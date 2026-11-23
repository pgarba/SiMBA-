// Folding of values that don't change with a function's free inputs.
//
// A lifted trace seeded with the live registers depends on only a few inputs
// (the stack pointer, typically). Obfuscators build opaque predicates and
// opaque constants from exactly those inputs -- e.g. C1*x*x == C2*y*y with x
// and y made from the stack address, never equal for an aligned stack -- and
// spread them over SIMD lanes, so neither LLVM's known bits nor an MBA
// simplifier proves them constant. This pass evaluates every integer (and
// integer vector) instruction at many random values of the free inputs, with
// a per-input alignment constraint (RSP mod 16 == 8 at a function entry), and
// replaces each value that came out the same every time by that constant.
//
// The result is sampled, not proved: a value that differs for very few inputs
// (fewer than 1 in the sample count) could be folded wrongly.
#ifndef SIMBA_INVARIANT_FOLD_H
#define SIMBA_INVARIANT_FOLD_H

namespace llvm {
class Module;
class Function;
}  // namespace llvm

namespace LSiMBA {

// With -fold-invariant: fold the invariant values of F (or of every defined
// function of M when F is null). Returns how many values were replaced; 0
// without the option.
int foldInvariantsIfEnabled(llvm::Module &M, llvm::Function *F);

// -fold-invariant -fold-invariant-only: the caller skips the MBA stage.
bool foldInvariantsOnly();

}  // namespace LSiMBA

#endif
