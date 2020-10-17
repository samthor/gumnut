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
 * @fileoverview Low-level wrapper for blep. Does not use Node-specific APIs.
 */

const PAGE_SIZE = 65536;
const WRITE_AT = PAGE_SIZE * 2;
const ERROR_CONTEXT_MAX = 256;  // display this much text on either side
const TOKEN_WORD_COUNT = 6;

const safeEval = eval;  // try to avoid global side-effects with rename

export const noop = () => {};
const defaultHandlers = {callback: noop, open: noop, close: noop};

const decoder = new TextDecoder('utf-8');

const errorMap = new Map();
errorMap.set(-1, 'unexpected');
errorMap.set(-2, 'stack');
errorMap.set(-3, 'internal');
Object.freeze(errorMap);

export const types = Object.freeze({
  eof: 0,
  lit: 1,
  semicolon: 2,
  op: 3,
  colon: 4,
  brace: 5,
  array: 6,
  paren: 7,
  ternary: 8,
  close: 9,
  string: 10,
  regexp: 11,
  number: 12,
  symbol: 13,
  keyword: 14,
  label: 15,
  block: 16,
});

export const specials = Object.freeze({
  sameline: 1,
  declare: 2,
  top: 4,
  property: 8,
  change: 16,
  external: 32,
  destructuring: 64,
  lit: (1 << 30),
});

export const stacks = Object.freeze({
  null: 0,
  expr: 1,
  declare: 2,
  control: 3,
  block: 4,
  function: 5,
  class: 6,
  misc: 7,
  label: 8,
  export: 9,
  module: 10,
  inner: 11,
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

  if (instance.instance) {
    instance = instance;
  }

  // emscripten creates _post_instantiate to configure statics
  instance.exports.__post_instantiate?.();

  return {instance, memory};
}

/**
 * @param {!Promise<BufferSource>} modulePromise
 * @return {!Promise<blep.Harness>}
 */
export default async function build(modulePromise) {
  let {callback, open, close} = defaultHandlers;

  // These views need to be mutable as they'll point to a new WebAssembly.Memory when it gets
  // resized for a new run.
  let view = new Uint8Array(0);

  const imports = {
    _memset(s, c, n) {
      // nb. This only happens once per run in prsr.
      view.fill(c, s, s + n);
      return s;
    },

    _memchr(ptr, char, len) {
      const index = view.subarray(ptr, ptr + len).indexOf(char);
      if (index === -1) {
        return 0;
      }
      return ptr + index;
    },

    _blep_parser_callback() {
      callback();
    },

    _blep_parser_open(type) {
      // if specifically returns false, skip this stack
      return open(type) === false ? 1 : 0;
    },

    _blep_parser_close(type) {
      close(type);
    },
  };

  const {instance, memory} = await initialize(modulePromise, () => imports);
  const {exports: {
    _blep_parser_cursor: parser_cursor,
    _blep_parser_init: parser_init,
    _blep_parser_run: parser_run,
  }} = instance;

  const tokenAt = parser_cursor();
  if (tokenAt >= WRITE_AT) {
    throw new Error(`token in invalid location`);
  }

  let tokenView = new Int32Array(memory.buffer, tokenAt, TOKEN_WORD_COUNT);
  let inputSize = 0;

  const token = /** @type {blep.Token} */ ({
    void() {
      return tokenView[0] - WRITE_AT;
    },

    at() {
      return tokenView[1] - WRITE_AT;
    },

    length() {
      return tokenView[2];
    },

    lineNo() {
      return tokenView[3];
    },

    type() {
      return tokenView[4];
    },

    special() {
      return tokenView[5];
    },

    view() {
      return view.subarray(tokenView[1], tokenView[1] + tokenView[2]);
    },

    string() {
      return decoder.decode(view.subarray(tokenView[1], tokenView[1] + tokenView[2]));
    },

    stringValue() {
      if (tokenView[4] !== types.string) {
        throw new TypeError('Can\'t stringValue() on non-string');
      }
      const target = view.subarray(tokenView[1], tokenView[1] + tokenView[2]);

      switch (target[0]) {
        case 96:
          if (target[target.length - 1] == 96) {
            break;
          }
          // fall-through

        case 125:
          throw new TypeError('Can\'t stringValue() on template string with holes');
      }

      return safeEval(decoder.decode(target));
    },
  });

  return {
    token,

    /**
     * @param {number} size
     * @return {!Uint8Array}
     */
    prepare(size) {
      const memoryNeeded = WRITE_AT + size + 1;
      if (memory.buffer.byteLength < memoryNeeded) {
        memory.grow(Math.ceil((memoryNeeded - memory.buffer.byteLength) / PAGE_SIZE));
      }

      tokenView = new Int32Array(memory.buffer, tokenAt, TOKEN_WORD_COUNT);  // in 32-bit
      view = new Uint8Array(memory.buffer);
      view[WRITE_AT + size] = 0;  // null-terminate
      inputSize = size;

      return new Uint8Array(memory.buffer, WRITE_AT, size);
    },

    /**
     * @param {blep.Handlers} handlers
     */
    handle(handlers) {
      ({callback, open, close} = {callback, open, close, ...handlers});
    },

    /**
     * @param {function(): void} callback
     * @param {(function(number): boolean)=} stack
     */
    run() {
      let statements = 0;
      let ret = parser_init(WRITE_AT, inputSize);
      if (ret >= 0) {
        do {
          ret = parser_run();
          ++statements;
        } while (ret > 0);
      }

      // reset handlers
      ({callback, open, close} = defaultHandlers);

      if (ret === 0) {
        return statements;
      }
      const at = tokenView[1];
      const view = new Uint8Array(memory.buffer);

      // Special-case crash on a NULL byte. There was no more input.
      if (view[at] === 0) {
        throw new TypeError(`Unexpected end of input`);
      }

      // Otherwise, generate a sane error.
      const lineNo = tokenView[3];
      const {line, pos, offset} = lineAround(view, at, WRITE_AT);
      const errorType = errorMap.get(ret) || `(? ${ret})`;
      throw new TypeError(`[${lineNo}:${pos}] ${errorType}:\n${line}\n${'^'.padStart(offset + 1)}`);
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
 * @param {number} writeAt start of buffer
 * @return {{line: string, pos: number, offset: number}}
 */
function lineAround(view, at, writeAt) {
  let lineAt = at;
  while (--lineAt >= writeAt) {
    if (view[lineAt] === 10) {
      break;
    }
  }

  // move past '\n' or first byte, but enforce content min
  const actualLineAt = lineAt + 1;
  lineAt = Math.max(at - ERROR_CONTEXT_MAX, actualLineAt);

  let lineView = view.subarray(lineAt, lineAt + ERROR_CONTEXT_MAX * 2);
  const lineTo = lineView.findIndex((v) => v === 0 || v === 10);
  if (lineTo !== -1) {
    lineView = lineView.subarray(0, lineTo);
  }

  const decoder = new TextDecoder('utf-8');
  const line = decoder.decode(lineView);

  return {
    line,
    pos: at - actualLineAt,
    offset: at - lineAt,
  };
}
