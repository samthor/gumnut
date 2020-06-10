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
 * @fileoverview Node interface to a JS bundler.
 */

import {specials, types} from './wrap.js';
import wrapper from './node.js';

/**
 * Builds a method which rewrites imports from a passed filename.
 *
 * @return {function(string): !ReadableStream}
 */
export default async function rewriter() {
  const w = await wrapper();

  return async (files) => {
    // TODO: just prints info about files right now

    for (const f of files) {
      const {token, run} = w(f);

      const scopes = [{}];
      const tops = [0];
      let top = scopes[0];

      const lookup = (name) => {
        const len = scopes.length;
        for (let i = 0; i < len; ++i) {
          if (scopes[i][name]) {
            return true;
          }
        }
        return false;
      };

      const callback = (special) => {
        if (special & specials.declare) {
          const cand = token.string();
          const isTop = (special & specials.declareTop);
          if (isTop) {
            if (top[cand] === false) {
              // TODO: found hoisted use, might need to reparse
              // (the var was used before being declared as a top-var)
              console.warn(cand, 'hoisted');
            }
            top[cand] = true;
          } else {
            // nb. if we see "x" in a non-top scope, then "let x", this assumes the first is only
            // hoisted access (and this is a new var): it's a TDZ invalid access _anyway_
            const scope = scopes[0];
            scope[cand] = true;
          }
        } else if (token.type() === types.symbol) {
          const cand = token.string();
          const found = lookup(cand);
          if (!found) {
            // This is either a global, or its "var"-like is after this code. Record it as required
            // and it will be popped out of the stack until it's found.
            // TODO: If it _is_ found, then we probably need to do 2nd pass, hoisted.
            top[cand] = false;
          }
        }
      };

      const stack = (special) => {
        if (special) {
          const next = {};
          if (special & specials.stackTop) {
            tops.unshift(scopes.length);
            top = next;
          }
          scopes.unshift(next);
          return;
        }

        scopes.shift();
        if (tops[0] !== scopes.length) {
          return;
        }

        const previous = top;
        tops.shift();
        top = scopes[tops[0]];

        // Pop out any symbols we couldn't find, and hope the parent scope has them.
        for (const key in previous) {
          if (previous[key] === false) {
            if (top[key]) {
              throw new TypeError(`value ${key} should have been spotted`);
            }
            top[key] = false;
          }
        }
      };

      const s = run(callback, stack);

      for (const key in top) {
        if (!top[key]) {
          console.warn(key, 'global');
        }
      }

      await streamTo(s);  // otherwise we clobber ourselves
    }
  };
}


function streamTo(s, target = process.stdout) {
  s.pipe(target, {end: false});
  s.resume();
  return new Promise((r) => s.on('close', r));
}


(async function run() {
  const r = await rewriter();

  r(process.argv.slice(2));

}());
