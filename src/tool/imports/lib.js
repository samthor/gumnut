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

import * as common from '../../harness/common.js';
import buildHarness from '../../harness/node-harness.js';
import rewriter from '../../harness/node-rewriter.js';

// Set to true to allow all stacks to be parsed (even though we don't need to as modules are
// top-level). Useful for debugging.
const allowAllStack = false;

/**
 * @type {(stack: number) => boolean}
 */
const stack = allowAllStack ? () => true : (type) => type === common.stacks.module;

/**
 * Builds a method which rewrites imports from a passed filename into ESM found inside node_modules.
 *
 * This emits relative paths to node_modules, rather than absolute ones.
 *
 * @param {(importer: string) => (importee: string) => string|undefined} buildResolver
 * @return {Promise<(file: string, write: (part: Uint8Array) => void) => void>}
 */
export default async function buildModuleImportRewriter(buildResolver) {
  const harness = await buildHarness();
  const {token, run} = rewriter(harness);

  return (f, write) => {
    const resolver = buildResolver(f);
    const callback = () => {
      if (!(token.special() === common.specials.external && token.type() === common.types.string)) {
        return;
      }
      const out = resolver(token.stringValue());
      if (out && typeof out === 'string') {
        return JSON.stringify(out);
      }
    };
    return run(f, {callback, stack, write});
  };
}
