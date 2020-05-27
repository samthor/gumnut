#!/usr/bin/env node
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

import fs from 'fs';
import path from 'path';
import build from './wrap.js';

const PENDING_BUFFER_MAX = 1024 * 16;

export default async function resolver(resolve) {
  const source = path.join(path.dirname(import.meta.url.split(':')[1]), 'runner.wasm');
  const wasm = fs.readFileSync(source);

  const decoder = new TextDecoder('utf-8');
  const run = await build(wasm);

  return (f, stream) => {
    const fd = fs.openSync(f);
    const stat = fs.fstatSync(fd);

    let buffer = null;
    const prepare = (b) => {
      buffer = b;  // store for later
      const read = fs.readSync(fd, buffer, 0, stat.size, 0);
      if (read !== stat.size) {
        throw new Error(`did not read all bytes at once: ${read}/${stat.size}`);
      }
    };

    let sent = 0;

    run(stat.size, prepare, (p, len, line_no, type, special) => {

      if (special === 0) {

        if (p - sent > PENDING_BUFFER_MAX) {
          // send some data, we've gone through a lot
          stream.write(buffer.subarray(sent, p));
          sent = p;
        }

        return;
      }

      // bump to high water mark
      stream.write(buffer.subarray(sent, p));

      let view = buffer.subarray(p + 1, p + len - 1);

      // Filter out slashes.
      if (view.some((c) => c === 92)) {
        // take the slow train, choo choo
        let skip = false;
        view = view.filter((c, index) => {
          if (skip) {
            skip = false;
            return true;  // always allow
          }
          skip = (c === 92);
          return !skip;
        });
      }

      const s = decoder.decode(view);
      const out = resolve(s, f);

      stream.write(JSON.stringify(out), 'utf-8');
      sent = p + len;  // we've moved past the "original" string here
    });

    // send rest
    stream.write(buffer.subarray(sent, buffer.length));
  };
}


(async function () {
  const r = await resolver((importee, importer) => {
    return `:blah:${importee}`;
  });
  r(process.argv[2], process.stdout);
}());

