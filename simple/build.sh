#!/bin/bash

set -eu
clang -o simple *.c ../{token,utils}.c
