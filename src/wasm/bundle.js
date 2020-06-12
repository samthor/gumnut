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

import fs from 'fs';
import {specials, types} from './wrap.js';
import build from './wrap.js';
import path from 'path';
import stream from 'stream';


class Scope {
  constructor(parent) {
    this.parent = parent;
    this.vars = {};
  }

  _get(cand) {
    let found = this.vars[cand];
    if (found === undefined) {
      this.vars[cand] = found = {decl: false, at: []};
    }
    return found;
  }

  /**
   * @param {!Set<string>} toplevels where to mark global use
   */
  external(toplevels) {
    for (const cand in this.vars) {
      if (!this.vars[cand].decl) {
        toplevels.add(cand);
      }
    }
  }

  /**
   * @param {!Set<string>} toplevels that are already being used
   * @return {!Array<{at: number, cand: string, update: string}>} updates in reverse order
   */
  updates(toplevels) {
    let updates = [];

    for (const cand in this.vars) {
      const {decl, at} = this.vars[cand];

      // Not defined at the top-level, so it must be a global. Don't rename it.
      if (decl === false) {
        continue;
      }

      // We can use this variable name at the top-level, it's not already defined.
      if (!toplevels.has(cand)) {
        toplevels.add(cand);
        continue;
      }

      // Try to rename this top-level var. It's possible but odd that "foo$1" is already defined
      // (maybe output from another bundler), so increment until we find an empty slot.
      let index = 0;
      for (;;) {
        let update = `${cand}$${++index}`;
        if (toplevels.has(update)) {
          continue;
        }
        updates = updates.concat(at.map((at) => ({at, cand, update})));
        break;
      }
    }

    updates.sort(({at: a}, {at: b}) => b - a);
    return updates;
  }

  /**
   * Consume a previous child scope.
   *
   * @param {!Scope} child
   */
  consume(child) {
    for (const cand in child.vars) {
      const od = child.vars[cand];
      if (od.decl) {
        continue;
      }

      const d = this._get(cand);
      d.at = d.at.concat(od.at);
    }
  }

  /**
   * Mark a variable as being used. This only records if we're at the top level
   * or not already declared locally.
   *
   * @param {string} cand
   * @param {number} at
   */
  use(cand, at) {
    let d = this.vars[cand];
    if (d === undefined) {
      this.vars[cand] = {decl: false, at: [at]};
    } else if (this.parent === null || !d.decl) {
      d.at.push(at);
    }
  }

  /**
   * Declares a variable as being defined here.
   *
   * @param {string} cand
   * @param {number} at
   */
  declare(cand, at) {
    let d = this.vars[cand];
    if (d === undefined) {
      this.vars[cand] = d = {decl: true, at: [at]};
    } else {
      d.decl = true;
      d.at.push(at);
    }
  }
}



/**
 * Builds a method which rewrites imports from a passed filename.
 *
 * @return {function(string): !ReadableStream}
 */
export default async function rewriter() {
  const source = path.join(path.dirname(import.meta.url.split(':')[1]), 'runner.wasm');
  const wasm = fs.readFileSync(source);

  const {prepare, run} = await build(wasm);

  return async (files) => {
    const toplevels = new Set();

    const fileQueue = new Set();
    for (const f of files) {
      fileQueue.add(path.resolve(f));
    }
    const fileData = new Map();

    for (const f of fileQueue) {
      const fd = fs.openSync(f);
      const stat = fs.fstatSync(fd);

      let buffer = null;
      const token = prepare(stat.size, (b) => {
        buffer = b;
        fs.readSync(fd, buffer, 0, stat.size, 0);
      });

      const scopes = [new Scope(null)];
      const tops = [0];
      let top = scopes[0];
      let scope = scopes[0];

      const callback = (special) => {
        if (special & specials.modulePath) {
          // FIXME: avoid eval
          const other = eval(token.string());
          if (/^\.\.?\//.test(other)) {
            fileQueue.add(path.join(path.dirname(f), other));
          }
          return;
        }

        if (special & specials.declare) {
          const cand = token.string();
          const where = (special & specials.declareTop) ? top : scope;
          where.declare(cand, token.at());

          if (special & specials.declareTop) {
            if (cand in top.vars) {
              // This is a hoisted var use, we probably don't care.
            }
          }
          // nb. Treat "let" and "const" like "var" w.r.t. hoisting. Use like this is invalid (TDZ)
          // so there's nothing we can do anyway.

        } else if (token.type() === types.symbol) {
          const cand = token.string();
          scope.use(cand, token.at());
        }
      };

      const stack = (special) => {
        if (special) {
          const next = new Scope(scope);
          if (special & specials.stackTop) {
            tops.unshift(scopes.length);
            top = next;
          }
          scopes.unshift(next);
          scope = next;
          return;
        }

        const previous = scope;
        scopes.shift();
        scope = scopes[0];
        scope.consume(previous);

        if (tops[0] === scopes.length) {
          tops.shift();
          top = scopes[tops[0]];
        }
      };

      const s = run(callback, stack);
      top.external(toplevels);

      fileData.set(f, {top, buffer});
    }
    console.warn('globals', toplevels);

    for (const {top, buffer} of fileData.values()) {
      const updates = top.updates(toplevels);
      const readable = new stream.Readable({emitClose: true});

      let at = 0;
      while (updates.length) {
        const next = updates.pop();

        readable.push(buffer.subarray(at, next.at));
        readable.push(next.update);
        at = next.at + next.cand.length;  // FIXME: length only works for utf-8
      }
      readable.push(buffer.subarray(at));

      readable.push(null);
      await streamTo(readable);
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
