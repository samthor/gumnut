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

import * as stream from 'stream';

import * as common from '../../harness/common.js';
import buildHarness from '../../harness/node-harness.js';
import rewriter from '../../harness/node-rewriter.js';
import {resolver} from './resolver.js';

// Set to true to allow all stacks to be parsed (even though we don't need to as modules are
// top-level). Useful for debugging.
const allowAllStack = false;

/**
 * Builds a method which rewrites imports from a passed filename into ESM found inside node_modules.
 *
 * This emits relative paths to node_modules, rather than absolute ones.
 *
 * TODO(samthor): Allow callers to intercept node_modules strings before emitting them.
 *
 * @return {Promise<(file: string) => stream.Readable>}
 */
export async function buildModuleImportRewriter() {
  return buildCustomModuleImportRewriter(resolver);
}

/**
 * Builds a method which rewrites imports from a passed filename.
 *
 * @param {(importee: string, importer: string) => string|void} resolver new import or void to retain
 * @return {Promise<(file: string) => stream.Readable>}
 */
export async function buildCustomModuleImportRewriter(resolver) {
  const harness = await buildHarness();
  const {token, run} = rewriter(harness);

  /**
   * @param {number} type
   */
  const stack = (type) => allowAllStack || type === common.stacks.module;

  return (f) => {
    const callback = () => {
      if (token.special() === common.specials.external && token.type() === common.types.string) {
        const out = resolver(token.stringValue(), f);
        if (out && typeof out === 'string') {
          return JSON.stringify(out);
        }
      }
    };
    return run(f, {callback, stack});
  };
}
