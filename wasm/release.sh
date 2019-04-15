#!/bin/bash

set -eu

./wasm.sh release

mkdir -p ../dist
cp runner.wasm index.html utils.js ../dist

git checkout gh-pages
cp ../dist/* ..

git add ../
git commit -m "release on $(date)"

