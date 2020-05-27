#!/usr/bin/env node

import fs from 'fs';
import path from 'path';
import build from './wrap.js';

function resolve(s) {
  return ':foo:' + s;
}

const PENDING_BUFFER_MAX = 1024 * 16;

export default async function resolver() {
  const source = path.join(path.dirname(import.meta.url.split(':')[1]), 'runner.wasm');
  const wasm = fs.readFileSync(source);

  const decoder = new TextDecoder('utf-8');
  const run = await build(wasm);

  return (f, stream) => {
    // TODO: use readSync to read directly into target buffer
    const buffer = fs.readFileSync(f);

    const fd = fs.openSync(f);
    const stat = fs.fstatSync(fd);

    const prepare = (buffer) => {
      const read = fs.readSync(fd, buffer, 0, stat.size, 0);
      console.info('read', read, 'of', stat.size);
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

      const view = buffer.subarray(p + 1, p + len - 1);
      const s = decoder.decode(view);  // TODO: could have " or etc within

      const out = resolve(s);
//      process.stderr.write(`???found import/export: ${s}, ${out}\n`);

      stream.write(JSON.stringify(out), 'utf-8');
      sent = p + len;  // we've moved past the "original" string here
    });

    // send rest
    stream.write(buffer.subarray(sent, buffer.length));
  };
}


(async function () {
  const r = await resolver();
  r(process.argv[2], process.stdout);
}());

