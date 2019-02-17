#!/bin/bash

set -eu
clang -o demo demo.c ../{token,utils}.c stream.c
