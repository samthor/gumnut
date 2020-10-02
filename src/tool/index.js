
import {processFile} from './split.js';
import build from '../harness/node-harness.js';
import fs from 'fs';

const runner = await build();
const sources = process.argv.slice(2);

sources.forEach((source) => {
  const buffer = fs.readFileSync(source);
  processFile(runner, buffer);
});

const foo = () => {};

try {
  global;
} catch (e) {
  // blah
}

// export function foo() {
//   var q = 123;
// }

// foo();


function foo2() {

}