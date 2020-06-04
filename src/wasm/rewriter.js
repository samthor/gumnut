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
 * @fileoverview Node interface to a JS imports rewriter.
 */

import fs from 'fs';
import path from 'path';
import build, {stringFrom, specials} from './wrap.js';
import stream from 'stream';

const PENDING_BUFFER_MAX = 1024 * 16;

/**
 * Builds a method which rewrites imports from a passed filename.
 *
 * @param {function(string, string): string} resolve passed importee and importer, return new import
 * @return {function(string): !ReadableStream}
 */
export default async function rewriter(resolve) {
  const source = path.join(path.dirname(import.meta.url.split(':')[1]), 'runner.wasm');
  const wasm = fs.readFileSync(source);

  const {prepare, run} = await build(wasm);

  return (f) => {
    const fd = fs.openSync(f);
    const stat = fs.fstatSync(fd);
    const readable = new stream.Readable();

    let buffer = null;
    const token = prepare(stat.size, (b) => {
      buffer = b;

      const read = fs.readSync(fd, buffer, 0, stat.size, 0);
      if (read !== stat.size) {
        throw new Error(`did not read all bytes at once: ${read}/${stat.size}`);
      }
    });

    let sent = 0;

    run((special) => {
      const p = token.at();

      if (special !== specials.modulePath) {
        if (p - sent > PENDING_BUFFER_MAX) {
          // send some data, we've gone through a lot
          readable.push(buffer.subarray(sent, p));
          sent = p;
        }
        return;
      }

      const len = token.len();
      const s = stringFrom(buffer.subarray(p + 1, p + len - 1));
      const out = resolve(s, f);
      if (out == null || typeof out !== 'string') {
        return;  // do nothing, data will be sent later
      }

      // bump to high water mark
      readable.push(buffer.subarray(sent, p));

      // write new import
      readable.push(JSON.stringify(out), 'utf-8');

      // move past the "original" string
      sent = p + len;
    });

    // send rest
    readable.push(buffer.subarray(sent, buffer.length));
    readable.push(null);
    return readable;
  };
}

