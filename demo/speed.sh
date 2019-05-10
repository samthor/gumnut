#!/bin/bash

cd "${BASH_SOURCE%/*}" || exit

set -eu
clang -O3 -o _runner ../*.c demo.c -DSPEED
time ./_runner
rm _runner
