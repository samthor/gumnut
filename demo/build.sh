#!/bin/bash

set -eu
gcc -o demo demo.c ../{token,parser,utils}.c
