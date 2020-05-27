#!/bin/bash

set -eu

cd "${BASH_SOURCE%/*}" || exit
clang -o _runner ../*.c demo.c

for X in $1/*; do
  {
    ./_runner < $X >/dev/null 2>/dev/null
  } || {
    echo "fail: $X"
  }
done

