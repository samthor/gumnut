#!/bin/bash

set -eu

FLAGS="-O1 -g4"
export EMCC_DEBUG=1
if [[ "${1-}" == "release" ]]; then
  export EMCC_DEBUG=0
  FLAGS="-O2 -DSPEED"
  echo "Release mode (\"${FLAGS}\")" >&2
elif [[ "${1-}" != "" ]]; then
  echo "Unknown mode: $1" >&2
  exit 1
fi

# With Homebrew on Mac as of 2020-06, this generates a warning like:
#
# > emcc: warning: the fastomp compiler is deprecated.  Please switch to the upstream llvm backend as soon as possible and open issues if you have trouble doing so [-Wfastcomp]
#
# This seems (unfortunately) intended for now as Homebrew doesn't yet support the "new" backend
# (stuck on fastcomp). There may also be other changes in emcc's output for this, probably around
# where it decides to place statics (fastcomp is at end, which is slow as we have to deref).

# nb. we need SIDE_MODULE mode 2, as it limits exports.

# TOTAL_MEMORY/TOTAL_STACK are set to a single page each: we put stack at the end of memory
# (Emscripten is dumb and stack would go forever otherwise) and this lets us pass as much memory as
# we like on creation inside JS.

# use two pages (65536 * 2) for memory, random number for stack
MEMORY=65536
STACK=2048

emcc $FLAGS \
  -s SIDE_MODULE=2 \
  -s ALLOW_MEMORY_GROWTH=0 \
  -s SUPPORT_LONGJMP=0 \
  -s ERROR_ON_UNDEFINED_SYMBOLS=0 \
  -s INITIAL_MEMORY=${MEMORY} \
  -s TOTAL_STACK=${STACK} \
  -o runner.wasm \
  *.c ../*.c

echo "Ok! => runner.wasm"
