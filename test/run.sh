#!/bin/bash

set -eu
clang test.c ../*.c -o _tester
./_tester
rm _tester
