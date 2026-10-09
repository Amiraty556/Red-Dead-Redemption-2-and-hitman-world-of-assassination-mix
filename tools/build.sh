#!/usr/bin/env bash
# Build ArthurContracts.asi - static runtime so players need nothing extra.
set -euo pipefail
CXX="$HOME/mingw64/bin/g++.exe"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MH="$HOME/minhook"
OUT="$ROOT/build"
mkdir -p "$OUT"

"$CXX" -std=c++17 -O2 -g -shared \
  -static-libgcc -static-libstdc++ \
  -I"$MH/include" \
  "$ROOT/src/main.cpp" \
  "$MH/src/hook.c" "$MH/src/trampoline.c" "$MH/src/buffer.c" "$MH/src/hde/hde64.c" \
  -o "$OUT/ArthurContracts.asi" \
  -lole32 -lpsapi

echo "built: $OUT/ArthurContracts.asi ($(stat -c %s "$OUT/ArthurContracts.asi") bytes)"
