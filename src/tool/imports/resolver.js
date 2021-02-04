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
import * as fs from 'fs';
import {resolve as internalResolver} from './node-resolve.js';


// Allow files or "/index." missing these suffixes. Update if you hate mjs.
const extToCheck = ['js', 'mjs'];


// Regexp that matches "../", "./" or "/" as a prefix.
const relativeRegexp = /^\.{0,2}\//;


/**
 * @param {string} p
 */
const statOrNull = (p) => {
  try {
    return fs.statSync(p);
  } catch (e) {
    return null;
  }
};


/**
 * @param {string} pathname
 * @return {string=}
 */
function confirmPath(pathname) {
  const stat = statOrNull(pathname);
  if (stat === null) {
    // Look for a file with a suffix.
    for (const ext of extToCheck) {
      const check = `${pathname}.${ext}`;
      const stat = statOrNull(check);
      if (stat && !stat.isDirectory()) {
        return check;
      }
    }
  } else if (stat.isDirectory()) {
    // Look for index.js in the directory.
    for (const ext of extToCheck) {
      const check = path.join(pathname, `index.${ext}`);
      const stat = statOrNull(check);
      if (stat && !stat.isDirectory()) {
        return check;
      }
    }
  } else {
    return pathname;
  }
}


/**
 * @type {blep.Resolver}
 */
export const resolver = (importee, importer) => {
  try {
    new URL(importee);
    return; // ignore, is valid URL
  } catch {}

  /** @type {string=} */
  let pathname;

  const absoluteImporter = new URL(path.resolve(importer), import.meta.url);
  const resolved = internalResolver(importee, absoluteImporter.toString());
  if (resolved !== undefined) {
    // We get back file:// URLs, beacause Node doesn't care about our webserver.
    const resolvedUrl = new URL(resolved);
    if (resolvedUrl.protocol !== 'file:') {
      throw new Error(`expected file:, was: ${resolvedUrl.toString()}`);
    }
    ({pathname} = resolvedUrl);
  } else {
    const absoluteUrl = new URL(importee, absoluteImporter);
    ({pathname} = absoluteUrl);
  }

  // Confirm the path actually exists (with extra node things).
  pathname = confirmPath(pathname);
  if (pathname === undefined) {
    return;
  }

  // Find the relative path from the request.
  const importerDir = path.dirname(absoluteImporter.pathname)
  const out = path.relative(importerDir, pathname);
  if (!relativeRegexp.test(out)) {
    return `./${out}`;  // don't allow naked pathname
  }
  return out;
};
