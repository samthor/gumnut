#!/bin/bash

set -eu
clang demo2.c ../core/token.c $@ -o _runner

