#!/bin/bash

set -eu
gcc -o demo demo.c ../{stream,token,utils}.c
