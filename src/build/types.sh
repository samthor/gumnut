#!/bin/bash

set -eu

# Build the broad set of types, but copy the only hand-written part into place.
rm -rf generatedTypes
tsc index.js --declaration --allowJs --emitDeclarationOnly --outDir generatedTypes
cp src/harness/types/index.d.ts generatedTypes/src/harness/types/

# TypeScript 4.1.3 (and possibly later) doesn't support types for subpath imports.
rm -rf imports/
mkdir -p imports/
echo "export * from '../src/tool/imports/lib';" > imports/index.d.ts
echo "export {default} from '../src/tool/imports/lib';" >> imports/index.d.ts
