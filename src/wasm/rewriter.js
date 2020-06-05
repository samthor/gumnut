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

import {specials} from './wrap.js';
import wrapper from './node.js';

/**
 * Builds a method which rewrites imports from a passed filename.
 *
 * @param {function(string, string): string} resolve passed importee and importer, return new import
 * @return {function(string): !ReadableStream}
 */
export default async function rewriter(resolve) {
  const w = await wrapper();

  return (f) => {
    const {token, run} = w(f);

    return run((special) => {
      if (special !== specials.modulePath) {
        return;
      }
      const s = token.string();
      const out = resolve(s.substr(1, s.length - 2), f);
      if (out && typeof out === 'string') {
        return out;
      }
    });
  };
}

