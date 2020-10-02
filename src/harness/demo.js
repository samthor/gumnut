#!/usr/bin/env node
/*
 * Copyright 2020 Sam Thorogood.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */

/**
 * @fileoverview Node interface to a JS imports rewriter.
 */

import * as harness from './harness.js';
import rewriter from './node-rewriter.js';
import * as lit from '../tokens/lit.mjs';
import {performance} from 'perf_hooks';

let prepTime = 0.0;
const runTimes = [];

/**
 * Builds a method which rewrites imports from a passed filename.
 *
 * @param {function(string, string): string} resolve passed importee and importer, return new import
 * @return {function(string): !ReadableStream}
 */
async function moduleImportRewriter(resolve) {
  const prepStart = performance.now();
  const w = await rewriter();
  prepTime = performance.now() - prepStart;

  return (f) => {
    const runStart = performance.now();
    const {token, run} = w(f);

    const callback = () => {
      if (token.special() === harness.specials.external && token.type() === harness.types.string) {
        const s = token.string();
        const out = resolve(s.substr(1, s.length - 2), f);
        if (out && typeof out === 'string') {
          return JSON.stringify(out);
        }
      }
    };
    const stack = (type) => {
      if (type === harness.stacks.external) {
        return 0;
      }
      if (type === harness.stacks.module && token.special() === lit.IMPORT) {
        return 0;
      }
      return 1;
    };

    try {
      return run(callback, stack);
    } finally {
      runTimes.push(performance.now() - runStart);
    }
  };
}


const alreadyResolved = /^(\w+:\/\/|\.{0,2}\/)/;



const run = await moduleImportRewriter((importee, importer) => {
  return 'LOL: ' + importee;

  // importee is from import/export, importer is the current file
  // TODO: this is a terrible resolver, don't do this
  if (!alreadyResolved.exec(importee)) {
    return `/node_modules/${importee}/index.js`;
  }
});

const targets = process.argv.slice(2);
if (!targets.length) {
  targets.push('demo.js');
}
targets.forEach((target) => {
  const s = run(target);
  s.pipe(process.stdout);
});

process.stderr.write(`prep ${prepTime.toFixed(2)}ms\n`);
runTimes.forEach((time) => {
  process.stderr.write(`run  ${time.toFixed(2)}ms\n`);
});
