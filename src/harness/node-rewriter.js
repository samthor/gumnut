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
 * @fileoverview Provides a wrapper for modifying JS with Node.
 */

import fs from 'fs';
import path from 'path';
import build from './harness.js';
import stream from 'stream';

const PENDING_BUFFER_MAX = 1024 * 16;

/**
 * @return {!Promise<function(string): blep.Base>}
 */
export default async function wrapper() {
  const source = path.join(path.dirname(import.meta.url.split(':')[1]), 'runner.wasm');
  const wasm = fs.readFileSync(source);

  const {prepare, run: internal, token, push, pop} = await build(wasm);

  return (f) => {
    // We want to stat the file so that we can create the correctly sized buffer and then _not_
    // have to move the source code around.

    const fd = fs.openSync(f);
    const stat = fs.fstatSync(fd);
    const readable = new stream.Readable({emitClose: true});

    const buffer = prepare(stat.size);
    const read = fs.readSync(fd, buffer, 0, stat.size, 0);
    if (read !== stat.size) {
      throw new Error(`did not read all bytes at once: ${read}/${stat.size}`);
    }

    const run = (callback, stack) => {
      let sent = 0;

      internal((special) => {
        const p = token.at();

        const update = callback(special);
        if (update === undefined) {
          if (p - sent > PENDING_BUFFER_MAX) {
            // send some data, we've gone through a lot
            readable.push(buffer.subarray(sent, p));
            sent = p;
          }
          return;
        }

        // bump to high water mark
        readable.push(buffer.subarray(sent, p));

        // write update
        readable.push(update, 'utf-8');

        // move past the "original" string
        sent = p + token.length();
      }, stack);

      // send rest
      readable.push(buffer.subarray(sent, buffer.length));
      readable.push(null);
      return readable;
    };

    return {token, run, push, pop};
  };

}