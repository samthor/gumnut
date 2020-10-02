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
 * @fileoverview Splits an incoming JS file into unrelated functionality.
 */

import {Scope, Registry} from './lib/vars.js';
import {stacks, specials, types} from '../harness/harness.js';
import * as lit from '../tokens/lit.mjs';

/**
 * Performs mechanical processing of the passed file for use in module-land by splitting into
 * all unrelated chunks. This takes ownership of the the passed buffer.
 *
 * @param {blep.Harness} runner
 * @param {!Buffer} buffer
 */
export function processFile(runner, buffer) {
  const {token} = runner;
  runner.prepare(buffer.length).set(buffer);

  const root = new Scope(0, 0);
  const scopes = [root];
  const tops = [0];
  let top = scopes[0];
  let scope = scopes[0];

  const stackHandler = (type) => {
    switch (type) {
      case stacks.null: {
        if (--scope.depth) {
          return true;  // nothing to do
        }
        const previous = scope;
        scopes.shift();
        scope = scopes[0];
        scope.consume(previous);

        if (tops[0] === scopes.length) {
          tops.shift();
          top = scopes[tops[0]];
        }
        return true;
      }

      case stacks.function: {
        // function which creates var scope
        const next = new Scope(type, token.at());
        tops.unshift(scopes.length);
        scopes.unshift(next);
        top = scope = next;
        return true;
      }

      case stacks.control: {
        // something that creates let/const scope
        const next = new Scope();
        scopes.unshift(next);
        scope = next;
        return true;
      }

      default: {
        ++scope.depth;  // this doesn't effect variable placement
        return true;
      }
    }

    switch (type) {
      case stacks.module:
        // TODO: wrapper code should handle pushing for _this_

        // const start = token.at();
        // const defaultExportHoist = Boolean(special & specials.defaultHoist);

        // if (token.hash() === hashes.import) {
        //   importHandler(runner, importCallback.bind(null, start));

        // } else {
        //   exportHandler(runner, exportCallback.bind(null, start, defaultExportHoist));

        // }
        // console.warn('FOUND module', token.string());
    }
  };

  const defaultCallbackHandler = () => {
    const special = token.special();
    if (token.type() == types.close) {
      return;
    }

    if (special & specials.declare) {
      const cand = token.string();
      const where = (special & specials.top) ? top : scope;
      where.mark(token, true);

      if (special & specials.top) {
        if (tops.length === 2 && top.type === stacks.function) {
          // console.warn('got a declared function statement inside top', `'${cand}'`, scopes);
        }

        if (cand in top.vars) {
          // This is a hoisted var use, we probably don't care.
        }
      }
      // nb. Treat "let" and "const" like "var" w.r.t. hoisting. Use like this is invalid (TDZ)
      // so there's nothing we can do anyway.

      // // Special-case top-level declarations (these are part of import/export but report here).
      // if (special & specials.external) {
      //   exports[cand] = cand;
      // } else if (special & specials.defaultHoist) {
      //   exports['default'] = cand;  // can be blank string, e.g. "export default function() {}"
      // }

    } else if (token.type() === types.symbol) {
      scope.mark(token, false);
    }
  };

  // Parse and mark externals. We can do this immediately, and there's no race conditions about
  // which file's globals "wins": the union of globals must be treated as global.
  runner.run(defaultCallbackHandler, stackHandler);
  const {externals, locals} = root.split();

  console.info('parsed file, externals=', externals, 'locals=', locals);
}
