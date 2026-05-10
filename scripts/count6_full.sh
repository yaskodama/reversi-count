#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INPUT="${1:-"$ROOT_DIR/examples/start6.txt"}"
if [[ $# -gt 0 ]]; then
  shift
fi
BUILD_DIR="$ROOT_DIR/build"
BINARY="$BUILD_DIR/count6_full"
SOURCE="$ROOT_DIR/tools/count6_full.cpp"

mkdir -p "$BUILD_DIR"

if command -v clang++ >/dev/null 2>&1; then
  CXX="${CXX:-clang++}"
elif command -v g++ >/dev/null 2>&1; then
  CXX="${CXX:-g++}"
else
  echo "error: clang++ or g++ is required" >&2
  exit 1
fi

CXXFLAGS="${CXXFLAGS:--std=c++17 -O3 -DNDEBUG -march=native}"

echo "building: $CXX $CXXFLAGS $SOURCE"
"$CXX" $CXXFLAGS "$SOURCE" -o "$BINARY"

echo "input: $INPUT"
echo "search: 6x6 full game count with bitboard + transposition cache"
echo "progress is printed every 10 seconds; current/deepest show the move number being searched."
echo

"$BINARY" "$INPUT" "$@"
