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

import * as path from 'path';
import * as fs from 'fs';
import {resolve as internalResolver} from './node-resolve.js';


/**
 * Allow files or "/index." missing these suffixes. Update if you hate mjs.
 */
const extToCheck = ['js', 'mjs'];


/**
 * Regexp that matches "../", "./" or "/" as a prefix.
 */
const relativeRegexp = /^\.{0,2}\//;


/**
 * Regexp that matches ".js" as a suffix.
 */
const matchJsSuffixRegexp = /\.js$/;


/**
 * Zero JS "file" that evaluates correctly.
 */
const zeroJsDefinitionsImport = 'data:text/javascript;charset=utf-8,/* was .d.ts only */';


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
 * @param {string} p
 * @return {boolean}
 */
const statIsFile = (p) => statOrNull(p)?.isFile() ?? false;


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
      if (statIsFile(check)) {
        return check;
      }
    }

    // Special-case .d.ts files when there's no better option... because TypeScript.
    //   - importing a naked or '.js' file allows the adjacent '.d.ts'
    //   - this file doesn't really exist, so return a zero import
    const tsCheck = [pathname + '.d.ts', pathname.replace(matchJsSuffixRegexp, '.d.ts')];
    for (const check of tsCheck) {
      if (statIsFile(check)) {
        return zeroJsDefinitionsImport;
      }
    }

  } else if (stat.isDirectory()) {
    // Look for index.js in the directory.
    for (const ext of extToCheck) {
      const check = path.join(pathname, `index.${ext}`);
      if (statIsFile(check)) {
        return check;
      }
    }

    // Look for a solo index.d.ts in the directory, which TypeScript allows.
    if (statIsFile(path.join(pathname, 'index.d.ts'))) {
      return zeroJsDefinitionsImport;
    }

  } else {
    return pathname;
  }

  // In the failure case this returns undefined.
}


/**
 * @param {string[]} constraints exports constraints choices, used as OR
 * @param {string} importee
 * @param {string} importer
 */
export function resolver(constraints, importee, importer) {
  try {
    new URL(importee);
    return; // ignore, is valid URL
  } catch {}

  /** @type {string=} */
  let pathname;

  let suffix = '';

  const absoluteImporter = new URL(path.resolve(importer), import.meta.url);
  const resolved = internalResolver(constraints, importee, absoluteImporter.toString());
  if (resolved !== undefined) {
    // We get back file:// URLs, beacause Node doesn't care about our webserver.
    const resolvedUrl = new URL(resolved);
    if (resolvedUrl.protocol !== 'file:') {
      throw new Error(`expected file:, was: ${resolvedUrl.toString()}`);
    }
    ({pathname} = resolvedUrl);
    suffix = resolvedUrl.search + resolvedUrl.hash;
  } else {
    const absoluteUrl = new URL(importee, absoluteImporter);
    ({pathname} = absoluteUrl);
    suffix = absoluteUrl.search + absoluteUrl.hash;
  }

  // Confirm the path actually exists (with extra node things).
  pathname = confirmPath(pathname);
  if (pathname === undefined) {
    return;
  }
  try {
    // confirmPath might return a data: URL, so check here.
    new URL(pathname);
    return pathname;
  } catch (e) {
    // ignore
  }

  // Find the relative path from the request.
  const importerDir = path.dirname(absoluteImporter.pathname)
  let out = path.relative(importerDir, pathname);
  if (!relativeRegexp.test(out)) {
    out = `./${out}`;  // don't allow naked pathname
  }
  return out + suffix;
}


/**
 * The default resolver which prefers browser constraints.
 */
export const defaultBrowserResolver = resolver.bind(null, ['browser']);
