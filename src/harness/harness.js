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

import * as blep from './types/index.js';

const PAGE_SIZE = 65536;
const WRITE_AT = PAGE_SIZE * 2;
const ERROR_CONTEXT_MAX = 256;  // display this much text on either side
const TOKEN_WORD_COUNT = 6;

const safeEval = eval;  // try to avoid global side-effects with rename

export const noop = () => {};
/** @type {blep.Handlers} */
const defaultHandlers = {callback: noop, open: noop, close: noop};

const decoder = new TextDecoder('utf-8');

const errorMap = new Map();
errorMap.set(-1, 'unexpected');
errorMap.set(-2, 'stack');
errorMap.set(-3, 'internal');
Object.freeze(errorMap);

import {string as stringType} from './types/v-types.js';

/**
 * @param {Promise<BufferSource>|BufferSource} modulePromise
 * @param {blep.InternalImports} imports
 * @return {Promise<{
 *   instance: WebAssembly.Instance,
 *   memory: WebAssembly.Memory,
 *   calls: blep.InternalCalls,
 * }>}
 */
async function initialize(modulePromise, imports) {
  const memory = new WebAssembly.Memory({initial: 2});

  const env = {
    memory,
    __memory_base: PAGE_SIZE,  // put Emscripten 'stack' at start of memory, not really used
    ...imports,
  };
  const importObject = {env};

  const module = await modulePromise;
  const instantiatedSource = await WebAssembly.instantiate(module, importObject);
  const {instance} = instantiatedSource;

  // In the browser, the exports appear on instantiatedSource; in Node, they're on instance.
  // @ts-ignore
  const calls = /** @type {blep.InternalCalls} */ (instantiatedSource.exports || instance.exports);

  // emscripten creates __wasm_call_ctors to configure statics
  calls.__wasm_call_ctors();

  return {instance, memory, calls};
}

/**
 * @param {Promise<BufferSource>|BufferSource} modulePromise
 * @return {Promise<blep.Harness>}
 */
export default async function build(modulePromise) {
  let {callback, open, close} = defaultHandlers;

  // These views need to be mutable as they'll point to a new WebAssembly.Memory when it gets
  // resized for a new run.
  let view = new Uint8Array(0);

  /** @type {blep.InternalImports} */
  const imports = {
    memset(s, c, n) {
      // nb. This only happens once per run.
      view.fill(c, s, s + n);
      return s;
    },

    memchr(ptr, char, len) {
      const index = view.subarray(ptr, ptr + len).indexOf(char);
      if (index === -1) {
        return 0;
      }
      return ptr + index;
    },

    blep_parser_callback() {
      callback();
    },

    blep_parser_open(type) {
      // if specifically returns false, skip this stack
      return open(type) === false ? 1 : 0;
    },

    blep_parser_close(type) {
      close(type);
    },
  };

  const {memory, calls} = await initialize(modulePromise, imports);

  const {
    blep_parser_init: parser_init,
    blep_parser_run: parser_run,
    blep_parser_cursor: parser_cursor,
  } = calls;

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
      if (tokenView[4] !== stringType) {
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
     * @return {Uint8Array}
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
     * @param {Partial<blep.Handlers>} handlers
     */
    handle(handlers) {
      ({callback, open, close} = {callback, open, close, ...handlers});
    },

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
 * @param {Uint8Array} view
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
 * @param {Uint8Array} view
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
