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

import {parentPort, workerData} from 'worker_threads';
import * as imw from './import-meta-worker-shared.js';

const debug = false;

const enc = new TextEncoder();

const shared = /** @type {SharedArrayBuffer} */ (workerData);
const i32 = new Int32Array(shared, 0, 2);
const i8 = new Uint8Array(shared, 8);

/**
 * @param {{
 *   specifier: string,
 *   parent: string,
 *   shared: SharedArrayBuffer,
 * }} message
 */
const handler = async (message) => {
  const {specifier, parent} = message;

  let resolved;
  try {
    resolved = await import.meta.resolve(specifier, parent);
  } catch (e) {
    // can't resolve, ignore
    debug && console.warn('could not resolve', specifier, parent, e);
  }

  if (resolved) {
    const out = enc.encode(resolved);
    i8.set(out);
    i32[1] = out.length;
  }
  i32[0] = resolved ? imw.STATUS_SUCCESS : imw.STATUS_FAILURE;
  Atomics.notify(i32, 0);

  // nb. We don't use Atomics.store because nothing else is trying to blocking read.
};

parentPort.on('message', handler);
