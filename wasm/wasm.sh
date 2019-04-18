#!/bin/bash

set -eu

FLAGS="-O1 -g4"
export EMCC_DEBUG=1
if [[ "${1-}" == "release" ]]; then
  export EMCC_DEBUG=0
  FLAGS="-Os"
  echo "Release mode (\"${FLAGS}\")" >&2
elif [[ "${1-}" != "" ]]; then
  echo "Unknown mode: $1" >&2
  exit 1
fi

# needed if we're not SIDE_MODULE
#   -s ERROR_ON_UNDEFINED_SYMBOLS=0 \

# causes signature mismatch but no size/speed changes
#   -s ONLY_MY_CODE=1 \

# TOTAL_MEMORY/TOTAL_STACK are set to a single page each: we put stack at the end of memory
# (Emscripten is dumb and stack would go forever otherwise) and this lets us pass as much memory as
# we like on creation inside JS.

emcc $FLAGS \
  -s EMIT_EMSCRIPTEN_METADATA=1 \
  -s SIDE_MODULE=1 \
  -s TOTAL_MEMORY=65536 \
  -s TOTAL_STACK=65536 \
  -o runner.wasm \
  *.c ../*.c

echo "Ok! => runner.wasm"
