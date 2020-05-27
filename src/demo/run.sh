#!/bin/bash

cd "${BASH_SOURCE%/*}" || exit

set -eu
clang -o _runner ../*.c demo.c $@
./_runner
rm _runner
