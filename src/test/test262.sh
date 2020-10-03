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

  # Likely that this test uses `let` as a variable name, which is unsupported.
  echo '"use strict";' > _test.js
  cat $X >> _test.js
  if ! node _test.js; then
    echo "SKIP: $BASE (unsupported by nodeJS strict mode)"
    return
  fi

  echo "FAIL: $BASE"
  IS_FAILED=1
  FAILED=$((FAILED+1))
}

for X in ../../node_modules/test262-parser-tests/pass-explicit/*.js; do
  ./_test262 < $X || fail $X
  COUNT=$((COUNT+1))
done

for X in ../../node_modules/test262-parser-tests/pass/*.js; do
  ./_test262 < $X || fail $X
  COUNT=$((COUNT+1))
done


rm _test262

PASSED=$(($COUNT - $FAILED))

echo "test262 ($PASSED/${COUNT}), $SKIPPED skipped"

exit $IS_FAILED
