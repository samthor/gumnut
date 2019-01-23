#!/bin/bash

set -eu
clang -o demo demo.c ../{stream,token,utils}.c
