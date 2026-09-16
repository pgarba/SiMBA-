#!/usr/bin/env bash
# Build the standalone MBA (GAMBA native port) core + mba_cli with g++/clang++.
#
# The MBA core uses llvm::APInt (128-bit) for the Node constant field, so it
# needs the LLVM headers and links LLVMSupport. Override LLVM_CONFIG to point at
# a non-default llvm-config.
#
# Usage:  bash MBA/build.sh [extra cxxflags...]
set -euo pipefail

LLVM_CONFIG="${LLVM_CONFIG:-llvm-config-14}"
command -v "$LLVM_CONFIG" >/dev/null 2>&1 || LLVM_CONFIG="llvm-config"
LLVM_INC="$("$LLVM_CONFIG" --includedir)"
LLVM_CXXFLAGS="$("$LLVM_CONFIG" --cxxflags)"
LLVM_LIBS="$("$LLVM_CONFIG" --libs core support)"

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$here/build"
mkdir -p "$out"

# All MBA sources except the CLI driver (kept last, like CMake's
# MBA_SOURCES + mba_cli.cpp). Verify.cpp carries the fast-check / Z3-proof
# verification; without MBA_HAS_Z3 (not defined here) its proveEquivalent
# is a no-op, so no Z3 library is needed.
sources=()
for f in "$here"/*.cpp; do
  [ "$(basename "$f")" = "mba_cli.cpp" ] && continue
  sources+=("$f")
done
sources+=("$here/mba_cli.cpp")

# shellcheck disable=SC2086
g++ -std=c++17 -O2 -fPIC -I"$here" -I"$here/.." -I"$here/../include" -I"$LLVM_INC" $LLVM_CXXFLAGS \
  "${sources[@]}" \
  -o "$out/mba_cli" $LLVM_LIBS -ldl -lpthread

echo "Built: $out/mba_cli"
