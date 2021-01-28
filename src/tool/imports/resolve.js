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
import {Worker} from 'worker_threads';
import * as imw from './import-meta-worker-shared.js';
import * as fs from 'fs';
import {legacyResolve} from './legacy-resolve.js';


const extToCheck = ['js', 'mjs'];


const relativeRegexp = /^\.{0,2}\//;


/**
 * This builds a "sync" worker that calls out to `import.meta.resolve` where available.
 *
 * TODO(samthor): Maybe drop this, because it'll always prefer "node" import.
 *
 * @return {blep.Resolver}
 */
function buildImportMetaWorker() {
  // Use a shared, SharedArrayBuffer as even though this is a worker we fundamentally cannot call
  // it in async fashion since we block on Atomics. This whole file is NOT async.
  const shared = new SharedArrayBuffer(imw.RESOLVE_BUFFER_SIZE);
  const i32 = new Int32Array(shared, 0, 2);  // result, length
  const i8 = new Uint8Array(shared, 8);

  const u = new URL('import-meta-worker.mjs', import.meta.url);
  const w = new Worker(u, {workerData: shared});
  w.unref();

  w.on('error', (err) => Promise.reject(err));

  const dec = new TextDecoder('utf-8');

  return (specifier, parent) => {
    parent = parent.toString();  // flatten URL to string
    w.postMessage({specifier, parent});

    Atomics.wait(i32, 0, 0);
    const ok = i32[0] === imw.STATUS_SUCCESS;
    i32[0] = 0;  // clear result
    if (ok) {
      const length = i32[1];
      return dec.decode(i8.slice(0, length));
    }
    return undefined;
  };
}


let internalResolver;

try {
  // @ts-ignore
  import.meta.resolve('.');
  internalResolver = buildImportMetaWorker();
} catch (e) {
  internalResolver = legacyResolve;
}


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

    /** @type {string=} */
    let pathname;

    const absoluteImporter = new URL(path.join(root, importer), import.meta.url);
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
}
