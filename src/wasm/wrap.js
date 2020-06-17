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

const PAGE_SIZE = 65536;
const WRITE_AT = PAGE_SIZE * 2;

const decoder = new TextDecoder('utf-8');

const ERROR__UNEXPECTED = -1;
const ERROR__STACK = -2;
const ERROR__INTERNAL = -3;

const ERRORS = Object.freeze({
  [ERROR__UNEXPECTED]: 'unexpected',
  [ERROR__STACK]: 'stack',
  [ERROR__INTERNAL]: 'internal',
});

export const specials = Object.freeze({
  modulePath: 1,
  declare: 2,
  top: 4,
  property: 8,
  external: 16,
  stackInc: 32,
  defaultHoist: 64,
});

// nb. Just includes hashes used by bundlers.
export const hashes = Object.freeze({
  as: 389816320,
  default: 1046249491,
  export: 931962883,
  from: 657244160,
  import: 920461360,
  _comma: 134578176,
  _star: 134561792,
});

export const types = Object.freeze({
  eof: 0,
  unknown: 1,
  lit: 2,
  slash: 3,
  semicolon: 4,
  op: 5,
  colon: 6,
  brace: 7,
  array: 8,
  paren: 9,
  tBrace: 10,
  ternary: 11,
  close: 12,
  comment: 13,
  string: 14,
  regexp: 15,
  number: 16,
  symbol: 17,
  keyword: 18,
  label: 19,
});

async function initialize(modulePromise, callback) {
  const memory = new WebAssembly.Memory({initial: 2});

  const env = {
    memory,
    __memory_base: PAGE_SIZE,  // put Emscripten 'stack' at start of memory, not really used
  };
  const importObject = {env};

  const methods = await callback();
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

  return {instance, memory};
}

export default async function build(modulePromise) {
  const shadowed = [];  // shadowed callbacks
  let _callback = null;
  let _stack = null;

  const {instance, memory} = await initialize(modulePromise, () => {
    return {
      _memset(s, c, n) {
        // nb. This only happens once per run in prsr.
        new Uint8Array(memory.buffer).fill(c, s, s + n);
        return s;
      },

      _modp_callback(special) {
        _callback(special);
      },

      _modp_stack(special) {
        _stack(special);
      },
    };
  });

  const {exports} = instance;
  const tokenAt = exports._xx_token();
  if (tokenAt >= WRITE_AT) {
    throw new Error(`token in invalid location`);
  }

  // These views need to be mutable as they'll point to a new WebAssembly.Memory when it gets
  // resized for a new run.
  let view = new Uint8Array(0);
  let tokenView = new Int32Array(memory.buffer, tokenAt, 20 >> 2);

  return {

    token: {
      at() {
        return tokenView[0] - WRITE_AT;
      },

      length() {
        return tokenView[1];
      },

      lineNo() {
        return tokenView[2];
      },

      type() {
        return tokenView[3];
      },

      hash() {
        return tokenView[4];
      },

      view() {
        return view.subarray(tokenView[0], tokenView[0] + tokenView[1]);
      },

      string() {
        return decoder.decode(view.subarray(tokenView[0], tokenView[0] + tokenView[1]));
      },
    },

    /**
     * @param {number} size
     * @return {!Uint8Array}
     */
    prepare(size) {
      const memoryNeeded = WRITE_AT + size + 1;
      if (memory.buffer.byteLength < memoryNeeded) {
        memory.grow(Math.ceil((memoryNeeded - memory.buffer.byteLength) / PAGE_SIZE));
      }

      tokenView = new Int32Array(memory.buffer, tokenAt, 20 >> 2);  // in 32-bit
      view = new Uint8Array(memory.buffer);
      view[WRITE_AT + size] = 0;  // null-terminate

      return new Uint8Array(memory.buffer, WRITE_AT, size);
    },

    /**
     * @param {!function(number): void} callback to push
     */
    push(callback) {
      shadowed.push(_callback);
      _callback = callback;
    },

    /**
     * @return {!function(number): void} now active callback
     */
    pop() {
      if (shadowed.length) {
        _callback = shadowed.pop();
      }
      return _callback;
    },

    /**
     * @param {!function(number): void} callback
     * @param {(!function(number): void)=} stack
     */
    run(callback, stack=() => {}) {
      _callback = callback;
      _stack = stack;
      shadowed.splice(0, shadowed.length);  // clear previous shadowed callbacks

      let ret = exports._xx_init(WRITE_AT);
      if (ret >= 0) {
        do {
          ret = exports._xx_run();
        } while (ret > 0);
      }

      _callback = null;
      _stack = null;

      if (ret === 0) {
        return;
      }
      const at = tokenView[0];
      const view = new Uint8Array(memory.buffer);

      // Special-case crash on a NULL byte. There was no more input.
      if (view[at] === 0) {
        throw new TypeError(`Unexpected end of input`);
      }

      // Otherwise, generate a sane error.
      const lineNo = tokenView[2];
      const {line, index} = lineAround(view, at, WRITE_AT);
      throw new TypeError(`[${lineNo}:${index}] ${ERRORS[ret] || '?'}:\n${line}\n${'^'.padStart(index + 1)}`);
    },

  };
}

/**
 * @param {!Uint8Array} view
 * @return {string}
 */
export function stringFrom(view) {
  if (view.some((c) => c === 92)) {
    // take the slow train, choo choo, filter out slashes
    let skip = false;
    view = view.filter((c) => {
      if (skip) {
        skip = false;
        return true;  // always allow
      }
      skip = (c === 92);
      return !skip;
    });
  }

  return decoder.decode(view);
}

/**
 * @param {!Uint8Array} view
 * @param {number} at of error or character
 * @param {number} writeAt start of string
 * @return {string}
 */
function lineAround(view, at, writeAt) {
  // TODO: It might be useful not to return a huge line, if we're given a ~10mb file on one line.

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
