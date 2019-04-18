#!/bin/bash

set -eu
clang -o _runner ../*.c demo.c -DSPEED
time ./_runner
rm _runner
