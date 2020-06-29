#!/bin/bash

cd "${BASH_SOURCE%/*}" || exit

set -eu

clang test262.c ../*.c -o _test262

IS_FAILED=0
FAILED=0
COUNT=0
SKIPPED=0

function fail() {
  BASE=$(basename $X)

  if [ $BASE == "5ecbbdc097bee212.js" ] || [ "$BASE" == "c8565124aee75c69.js" ]; then
    # these use "let" as a variable name
    SKIP="invalid in strict mode"
  fi

  if [ "$SKIP" != "" ]; then
    echo "skip: $BASE ($SKIP)"
    COUNT=$((COUNT-1))
    SKIPPED=$((SKIPPED+1))
  else
    echo "FAIL: $BASE"
    IS_FAILED=1
    FAILED=$((FAILED+1))
  fi
}

for X in ../../node_modules/test262-parser-tests/pass-explicit/*.js; do
  # if [ $BASE == "5ecbbdc097bee212.js" ] || [ "$BASE" == "c8565124aee75c69.js" ]; then
  #   echo "skip: $BASE (invalid in strict mode)"
  #   continue
  # elif [ $BASE == "c7e5fba8bf3854cd.js" ]; then
  #   echo "skip: $BASE (null byte in string)"
  #   continue
  # fi

#echo "trying $X"
  ./_test262 < $X || fail $X
  COUNT=$((COUNT+1))
done

rm _test262

PASSED=$(($COUNT - $FAILED))

echo "test262 ($PASSED/${COUNT}), $SKIPPED skipped"

exit $IS_FAILED
