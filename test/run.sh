#!/bin/bash

set -eu

gcc -fnested-functions ../*.c *.c -o ./suite
./suite

