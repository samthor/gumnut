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

import * as fs from 'fs';
import * as path from 'path';
import {createRequire} from 'module';


/**
 * @param {string} importee
 * @return {{name?: string, rest: string}}
 */
export function splitImport(importee) {
  const name = importee.split('/', 1)[0];
  if (!name || name.startsWith('.')) {
    return {rest: importee};
  }
  return {name, rest: '.' + importee.substr(name.length)};
}


/**
 * @param {string} name
 * @param {string} importer
 * @return {{resolved?: string, info?: any}}
 */
export function loadPackage(name, importer) {
  const require = createRequire(importer);

  const candidatePaths = require.resolve.paths(name) ?? [];
  let packagePath;

  for (const p of candidatePaths) {
    const check = path.join(p, name, 'package.json');
    if (fs.existsSync(check)) {
      packagePath = check;
      break;
    }
  }
  if (!packagePath) {
    return {};
  }

  const info = JSON.parse(fs.readFileSync(packagePath, 'utf-8'));
  return {resolved: path.dirname(packagePath), info};
}


/**
 * @param {any} exports
 * @param {string} rest
 * @return {{node: any, subpath?: string}}
 */
function matchExportsNode(exports, rest) {
  if (typeof exports !== 'object') {
    return {node: exports};
  }
  let fallback;

  for (const key in exports) {
    if (key !== '.' && !key.startsWith('./')) {
      fallback = exports;  // it might be "import" and so on
      continue;
    }
    if (key === rest) {
      return {node: exports[key]};
    }

    // Look for keys ending with /*.
    if (!key.endsWith('/*')) {
      continue;
    }
    const prefix = key.substr(0, key.length - 1);
    if (!(rest.startsWith(prefix) && rest.length > prefix.length)) {
      continue;
    }
    const subpath = rest.substr(prefix.length);
    if (path.normalize(subpath) !== subpath) {
      continue;  // node prevents conditional escaping path or having "."
    }

    return {node: exports[key], subpath};
  }

  return {node: fallback};
}


/**
 * @param {string} importee relative or naked string
 * @param {string} importee absolute importer URL, will always start with "file://"
 * @return {string|void}
 */
export function resolve(importee, importer) {
  const {name, rest} = splitImport(importee);
  if (!name) {
    return;
  }

  const {resolved, info} = loadPackage(name, importer);
  if (!resolved) {
    return;
  }

  const exports = info['exports'];

  // Without an exports field, check a few legacy options.
  if (!exports) {
    let simple = rest;
    if (rest === '.') {
      simple = info['module'] ?? info['esnext'] ?? info['main'] ?? rest;
    }
    return `file://${path.join(resolved, simple)}`;
  }

  // Look for "." etc mappings.
  let {node, subpath} = matchExportsNode(exports, rest);

  // Traverse looking for the best conditional export.
  restart: while (node && typeof node !== 'string') {
    for (const key in node) {
      if (key === 'import' || key === 'browser') {
        node = node[key];
        continue restart;
      }
    }
    node = node['default'];
  }
  if (!node) {
    return;
  }

  if (subpath) {
    node = node.replace(/\*/g, subpath);
  }
  return `file://${path.join(resolved, node)}`;
}
