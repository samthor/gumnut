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

import * as blep from './types/index.js';
import * as fs from 'fs';
import * as stream from 'stream';
import build from './node-harness.js';
import {noop} from './harness.js';

const PENDING_BUFFER_MAX = 1024 * 16;

/**
 * @return {!Promise<{
 *   run(file: string, args?: blep.RewriterArgs): stream.Readable,
 *   token: blep.Token,
 * }>}
 */
export default async function wrapper() {
  const {prepare, token, run: internalRun, handle} = await build();

  /**
   * @param {string} f
   * @param {blep.RewriterArgs} args
   */
  const run = (f, {callback = noop, stack = noop}) => {
    const fd = fs.openSync(f, 'r');
    const stat = fs.fstatSync(fd);
    const readable = new stream.Readable();

    const buffer = prepare(stat.size);
    const read = fs.readSync(fd, buffer, 0, stat.size, 0);
    if (read !== stat.size) {
      throw new Error(`did not read all bytes at once: ${read}/${stat.size}`);
    }

    let sent = 0;

    handle({
      callback() {
        const p = token.at();

        const update = callback();
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
      },

      open: stack,

      close(type) {
        // nb. we're passed the type being closed
        stack(0);
      },
    });

    internalRun();

    // send rest
    readable.push(buffer.subarray(sent, buffer.length));
    readable.push(null);
    return readable;
  };

  return {
    run,
    token,
  };
}