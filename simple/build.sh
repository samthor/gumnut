#!/bin/bash

set -eu
clang -o simple simple.c demo.c ../{token,utils}.c
