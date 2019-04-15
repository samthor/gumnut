#!/bin/bash

set -eu

emcc -O1 \
  -g4 \
  -s WASM=1 \
  -s ONLY_MY_CODE=1 \
  -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
  --llvm-opts "['-disable-simplify-libcalls', '-O0']" -ffreestanding -fno-builtin \
  -s DEFAULT_LIBRARY_FUNCS_TO_INCLUDE="[]" \
  -o runner.js \
  *.c ../*.c ../simple/simple.c

#   -s LIBRARY_DEPS_TO_AUTOEXPORT="[]" \
