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
  
      _token_callback(p, len, line_no, type, special) {
        handler(p, len, line_no, type, special);
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
    const written = prepare(inner) ?? size;
    if (written > inner.length) {
      throw new Error(`got too many bytes: ${written}`);
    }
    view[writeAt + written] = 0;  // null-terminate

    exports._xx_setup(writeAt);
    handler = (p, len, lineNo, type, special) => {
      callback(p - writeAt, len, lineNo, type, special);
    };

    let ret = 0;
    for (;;) {
      ret = exports._xx_run();
      if (ret <= 0) {
        break;
      }
    }
    handler = null;

    if (ret < 0) {
      throw new TypeError(`internal error: ${ret}`);
    }
  };

  return run;
}

