#!/bin/bash

cd "${BASH_SOURCE%/*}" || exit

set -eu
clang -o _runner ../token2.c ../parser.c ../helper.c demo.c $@
./_runner
rm _runner
