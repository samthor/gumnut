#!/bin/bash

set -eu
emcc -O0 -s WASM=1 -s ONLY_MY_CODE=1 \
  --llvm-opts "['-disable-simplify-libcalls', '-O0']" -ffreestanding -fno-builtin \
  -s DEFAULT_LIBRARY_FUNCS_TO_INCLUDE="[]" \
  -s LIBRARY_DEPS_TO_AUTOEXPORT="[]" \
  -s EXPORTED_FUNCTIONS="['_prsr_setup', '_prsr_run', '_prsr_get_at', '_prsr_get_line_no', '_prsr_get_len', '_prsr_get_type']" \
  -o runner.js \
  runner.c ../*.c

