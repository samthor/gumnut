#!/bin/bash

set -eu
clang -o simple simple.c demo.c ../tokens/helper.c ../token.c $@
