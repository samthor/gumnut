#!/bin/bash

set -eu

FLAGS="-O1 -g4"
if [[ "$1" == "release" ]]; then
  FLAGS="-O3"
  echo "Release mode (\"${FLAGS}\")" >&2
elif [[ "$1" != "" ]]; then
  echo "Unknown mode: $1" >&2
  exit 1
fi

emcc $FLAGS \
  -s WASM=1 \
  -s ONLY_MY_CODE=1 \
  -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
  --llvm-opts "['-disable-simplify-libcalls', '-O0']" -ffreestanding -fno-builtin \
  -s DEFAULT_LIBRARY_FUNCS_TO_INCLUDE="[]" \
  -o runner.js \
  *.c ../*.c ../simple/simple.c
