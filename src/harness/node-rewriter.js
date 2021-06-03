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
 * @fileoverview Provides a wrapper for modifying on-disk JS with Node.
 *
 * This reads from disk as we can read the file directly into the WebAssembly memory reqiured by
 * blep, rather than copying it around.
 */

import * as blep from './types/index.js';
import * as fs from 'fs';
import {noop} from './harness.js';


const PENDING_BUFFER_MAX = 1024 * 16;
const encoder = new TextEncoder();


/**
 * @param {blep.Harness} harness
 * @return {blep.RewriterReturn}
 */
export default function wrapper(harness) {
  const {prepare, token, run: internalRun, handle} = harness;

  /**
   * @param {string} f
   * @param {Partial<blep.RewriterArgs>} args
   */
  const run = (f, {callback = noop, stack = noop, write = noop}) => {
    const fd = fs.openSync(f, 'r');
    /** @type {Uint8Array} */
    let buffer;
    try {
      const stat = fs.fstatSync(fd);

      buffer = prepare(stat.size);
      const read = fs.readSync(fd, buffer, 0, stat.size, 0);
      if (read !== stat.size) {
        throw new Error(`did not read all bytes at once: ${read}/${stat.size}`);
      }
    } finally {
      fs.closeSync(fd);
    }

    let sent = 0;

    handle({
      callback() {
        const p = token.at();

        const update = callback();
        if (update === undefined) {
          if (p - sent > PENDING_BUFFER_MAX) {
            // send some data, we've gone through a lot
            write(buffer.subarray(sent, p));
            sent = p;
          }
          return;
        }

        // bump to high water mark
        if (sent !== p) {
          write(buffer.subarray(sent, p));
        }

        // write update
        if (update.length) {
          if (typeof update === 'string') {
            write(encoder.encode(update));
          } else {
            write(update);
          }
        }

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
    if (sent !== buffer.length) {
      write(buffer.subarray(sent, buffer.length));
    }
  };

  return {
    run,
    token,
  };
}
