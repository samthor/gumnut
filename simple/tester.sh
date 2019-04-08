#!/bin/bash

clang test.c simple.c ../token.c ../utils.c -o tester && ./tester
rm tester
