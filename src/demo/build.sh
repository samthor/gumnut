#!/bin/bash

set -eu
clang demo.c ../core/*.c $@ -o _runner

