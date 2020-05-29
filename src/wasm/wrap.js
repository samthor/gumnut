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
 * @fileoverview Provides a builder for the module rewriter.
 */

const STACK_PAGES = 4;

const ERROR__UNEXPECTED = -1;
const ERROR__STACK = -2;
const ERROR__INTERNAL = -3;

const ERRORS = Object.freeze({
  [ERROR__UNEXPECTED]: 'unexpected',
  [ERROR__STACK]: 'stack',
  [ERROR__INTERNAL]: 'internal',
});

async function initialize(modulePromise, callback, pages = 128) {
  const memory = new WebAssembly.Memory({initial: pages, maximum: pages});
  const table = new WebAssembly.Table({initial: 2, maximum: 2, element: 'anyfunc'});
  const view = new Uint8Array(memory.buffer);

  const env = {
    memory,
    __memory_base: (pages - STACK_PAGES) * 65536,  // put Emscripten 'stack' at end of memory
    table,
    __table_base: 0,
  };
  const importObject = {env};

  const methods = await callback(view);
  for (const method in methods) {
    importObject.env[method] = methods[method];
  }

  const module = await modulePromise;
  let instance = await WebAssembly.instantiate(module, importObject);

  // emscripten _post_instantiate
  if (!instance.exports && instance.instance) {
    instance = instance.instance;
  }
  instance.exports.__post_instantiate && instance.exports.__post_instantiate();

  return {instance, view};
}

export default async function build(modulePromise) {
  const writeAt = 1024;
  let handler = null;

  const {instance, view} = await initialize(modulePromise, (view) => {
    return {
      _memset(s, c, n) {
        view.fill(c, s, s + n);
        return s;
      },
  
      _memcpy(dst, src, size) {
        view.set(view.subarray(src, src + size), dst);
        return dst;
      },
  
      abort(x) {
        throw x;
      },
  
      _token_callback(special) {
        handler(special);
      },
    };
  });

  const {exports} = instance;

  /**
   * @param {number} size
   * @param {function(!Uint8Array): void} prepare
   * @param {function(number, number, number, number, number): void} callback
   */
  const run = (size, prepare, callback) => {
    if (size >= view.length - (STACK_PAGES * 65536) - writeAt - 1) {
      throw new Error(`can't parse huge file: ${size}`);
    }

    const inner = view.subarray(writeAt, writeAt + size);
    let written = prepare(inner);
    if (written == null) {
      written = size;
    }
    if (written > inner.length) {
      throw new Error(`got too many bytes: ${written}`);
    }
    view[writeAt + written] = 0;  // null-terminate

    const tokenAt = exports._xx_setup(writeAt);
    const tokenView = new Int32Array(view.buffer, tokenAt, 20 >> 2);  // in 32-bit
    handler = (special) => {
      // nb. tokenView[4] is actually uint32_t, but it's not used anyway
      callback(tokenView[0] - writeAt, tokenView[1], tokenView[2], tokenView[3], special);
    };

    let ret = 0;
    for (;;) {
      ret = exports._xx_run();
      if (ret <= 0) {
        break;
      }
    }
    handler = null;

    if (ret >= 0) {
      return;  // no result, all via callbacks
    }
    const at = tokenView[0];

    // Special-case crash on a NULL byte. There was no more input.
    if (view[at] == 0) {
      throw new TypeError(`Unexpected end of input`);
    }

    // Otherwise, generate a sane error.
    const lineNo = tokenView[2];
    const {line, index} = lineAround(view, at, writeAt);
    throw new TypeError(`[${lineNo}:${index}] ${ERRORS[ret] || '?'}:\n${line}\n${'^'.padStart(index + 1)}`);
  };

  return run;
}

/**
 * @param {!Uint8Array} view
 * @param {number} at of error or character
 * @param {number} writeAt start of string
 * @return {string}
 */
function lineAround(view, at, writeAt) {
  let lineAt = at;
  while (--lineAt >= writeAt) {
    if (view[lineAt] === 10) {
      break;
    }
  }
  ++lineAt;  // move back past '\n' or first byte
  const indexAt = at - lineAt;

  let lineView = view.subarray(lineAt);
  const lineTo = lineView.findIndex((v) => v === 0 || v === 10);
  if (lineTo !== -1) {
    lineView = lineView.subarray(0, lineTo);
  }

  const decoder = new TextDecoder('utf-8');
  const line = decoder.decode(lineView);

  const temp = decoder.decode(lineView.subarray(0, indexAt));
  return {
    line,
    index: line.length - temp.length,
  };
}
