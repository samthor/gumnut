#!/bin/bash

set -eu

./wasm.sh release

mkdir -p ../dist
cp runner.wasm index.html utils.js ../dist

git checkout gh-pages
cp ../dist/* ..

git add $(find .. -type f -maxdepth 1)  # only files in top-level dir
git commit -m "release on $(date)"

