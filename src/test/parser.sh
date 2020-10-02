#!/bin/bash

cd "${BASH_SOURCE%/*}" || exit

set -eu
clang parser.c ../core/*.c -o _parser
./_parser
rm _parser
