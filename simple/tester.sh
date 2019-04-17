#!/bin/bash

clang test.c simple.c ../tokens/helper.c ../token.c -o tester && ./tester
rm tester
