#!/bin/bash

cd "${BASH_SOURCE%/*}" || exit

set -eu
clang test.c ../*.c -o _tester
./_tester
rm _tester
