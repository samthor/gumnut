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

function buildImportMetaWorker() {
  // Use a shared, SharedArrayBuffer as even though this is a worker we fundamentally cannot call
  // it in async fashion since we block on Atomics. This whole file is NOT async.
  const shared = new SharedArrayBuffer(imw.RESOLVE_BUFFER_SIZE);
  const i32 = new Int32Array(shared, 0, 2);  // result, length
  const i8 = new Uint8Array(shared, 8);

  const u = new URL('import-meta-worker.mjs', import.meta.url);
  const w = new Worker(u, {workerData: shared});

  w.on('error', (err) => Promise.reject(err));

  const dec = new TextDecoder('utf-8');

  /**
   * @param {string} specifier
   * @param {string|URL} parent
   * @return {string}
   */
  return (specifier, parent) => {
    parent = parent.toString();  // flatten URL to string
    w.postMessage({specifier, parent});

    Atomics.wait(i32, 0, 0);
    const ok = i32[0] === imw.STATUS_SUCCESS;
    if (ok) {
      const length = i32[1];
      return dec.decode(i8.slice(0, length));
    }
    return undefined;
  };
}


/**
 * @type {(specifier: string, parent: string|URL) => string|undefined}
 */
const internalResolver = (() => {
  try {
    import.meta.resolve('.');
    return buildImportMetaWorker();
  } catch (e) {
    // TODO(samthor): Write something the old fashioned way.
    throw new Error('unsupported');
  }
})();

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

    const absoluteImporter = new URL(path.join(root, importer), import.meta.url);
    const resolved = internalResolver(importee, absoluteImporter);
    if (resolved === undefined) {
      return;  // failed to resolve
    }

    // We get back file:// URLs, beacause Node doesn't care about our webserver.
    // Find the relative path to node_modules from the request.
    const resolvedUrl = new URL(resolved);
    if (resolvedUrl.protocol !== 'file:') {
      throw new Error(`expected file:, was: ${resolvedUrl.toString()}`);
    }
    const importerDir = path.dirname(absoluteImporter.pathname)
    return path.relative(importerDir, resolvedUrl.pathname);
  };
}

// console.info('TEST imported', internalResolver('viz-observer', import.meta.url));
