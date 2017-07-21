#!/bin/bash

set -eu
emcc -O0 -s WASM=1 -s ONLY_MY_CODE=1 \
  --llvm-opts "['-disable-simplify-libcalls', '-O0']" -ffreestanding -fno-builtin \
  -s DEFAULT_LIBRARY_FUNCS_TO_INCLUDE="[]" \
  -s LIBRARY_DEPS_TO_AUTOEXPORT="[]" \
  -o runner.js \
  *.c ../*.c

