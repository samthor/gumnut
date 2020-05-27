#!/bin/bash

set -eu

# check that branch switch is safe
git checkout gh-pages
git checkout master

# build
./wasm.sh release

# copy to temporary location
mkdir -p ../../dist
cp runner.wasm index.html utils.js ../../dist

# move into place
git checkout gh-pages
cp ../../dist/* ../..

# add files
git add $(find ../.. -type f -maxdepth 1)  # only files in top-level dir
git commit -m "release on $(date)"
git push

# back to master
git checkout master
