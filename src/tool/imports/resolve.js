/*
 * Copyright 2021 Sam Thorogood.
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

import * as blep from '../../harness/types/index.js';
import * as path from 'path';


const regexpValidPath = /^\.{0,2}\//;


/**
 * Builds a resolver function.
 *
 * @param {string} root dir, default cwd
 * @return {blep.Resolver}
 */
export function buildResolver(root = process.cwd()) {
  return (importee, importer) => {
    try {
      new URL(importee);
      return; // ignore, is valid URL
    } catch {}

    if (!regexpValidPath.test(importee)) {

      const pathname = path.join(root, path.dirname(importer));

      // TODO: weird blocking shit to extract this
      const cand = import.meta.resolve(importee, importer);


      return;  // valid
    }

    // TODO(samthor): the work
    return `/node_modules/${importee}/index.js`;

    // TODO(samthor): Look for missing '.js' or importing directories (imports index.js)
  };
}