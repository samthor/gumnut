#!/bin/bash

cd "${BASH_SOURCE%/*}" || exit

set -eu

clang test262.c ../core/*.c -o _test262

IS_FAILED=0
FAILED=0
COUNT=0
SKIPPED=0

function fail() {
  BASE=$(basename $X)

  if [ $BASE == "df696c501125c86f.js" ] ||
     [ $BASE == "c442dc81201e2b55.js" ] ||
     [ $BASE == "6b36b5ad4f3ad84d.js" ] ||
     [ $BASE == "6815ab22de966de8.js" ] ||
     [ $BASE == "2ef5ba0343d739dc.js" ] ; then
    # these use "let" as a variable name
    SKIP="invalid in strict mode"
  else
    SKIP=""
  fi

  if [ "$SKIP" != "" ]; then
    echo "skip: $BASE ($SKIP)"
    COUNT=$((COUNT-1))
    SKIPPED=$((SKIPPED+1))
  else
    echo "FAIL: $X"
    IS_FAILED=1
    FAILED=$((FAILED+1))
  fi
}

for X in ../../node_modules/test262-parser-tests/pass-explicit/*.js; do
  ./_test262 < $X || fail $X
  COUNT=$((COUNT+1))
done

rm _test262

PASSED=$(($COUNT - $FAILED))

echo "test262 ($PASSED/${COUNT}), $SKIPPED skipped"

exit $IS_FAILED
