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

import * as blep from './types/index.js';

import * as harness from './harness.js';
import rewriter from './node-rewriter.js';
import {performance} from 'perf_hooks';
import * as stream from 'stream';

// Set to true to allow all stacks to be parsed (even though we don't need to as modules are
// top-level).
const allowAllStack = false;

let prepTime = 0.0;
const runTimes = [];

/**
 * Builds a method which rewrites imports from a passed filename.
 *
 * @param {(importee: string, importer: string) => string} resolve passed importee and importer, return new import
 * @return {Promise<(file: string) => stream.Readable>}
 */
async function moduleImportRewriter(resolve) {
  const prepStart = performance.now();
  const {token, run} = await rewriter();
  prepTime = performance.now() - prepStart;

  return (f) => {
    const runStart = performance.now();

    /** @type {blep.RewriterArgs} */
    const args = {
      callback() {
        if (token.special() === harness.specials.external && token.type() === harness.types.string) {
          const out = resolve(token.stringValue(), f);
          if (out && typeof out === 'string') {
            return JSON.stringify(out);
          }
        }
      },

      stack(type) {
        return (allowAllStack || type === harness.stacks.module);
      },
    };

    try {
      return run(f, args);
    } finally {
      runTimes.push(performance.now() - runStart);
    }
  };
}


const run = await moduleImportRewriter((importee, importer) => {
  return 'update://' + importee;
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
