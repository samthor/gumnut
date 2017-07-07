#!/bin/bash

set -eu

gcc -fnested-functions ../{parser,token,utils}.c *.c -o ./suite
./suite

