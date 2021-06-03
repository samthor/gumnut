/*
 * Copyright 2021 Sam Thorogood.
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
 * Enum of valid token types.
 */
type TokenValues = typeof import('./v-types.js')[keyof typeof import('./v-types.js')];

/**
 * Enum of valid stack types.
 */
type StackValues = typeof import('./v-stacks.js')[keyof typeof import('./v-stacks.js')];

/**
 * Calls provided by the internal C code.
 */
export interface InternalCalls {
  __wasm_call_ctors(): void;

  blep_parser_init(at: number, len: number): number;
  blep_parser_run(): number;
  blep_parser_cursor(): number;
}

/**
 * Imports required by the internal C code, including standard library calls.
 */
export interface InternalImports {
  memset(at: number, byte: number, size: number): void;
  memchr(at: number, byte: number, size: number): number;

  blep_parser_callback(): void;
  blep_parser_open(type: StackValues): 0 | 1;
  blep_parser_close(type: StackValues): void;
}

/**
 * Handlers provided to the harness code.
 */
export interface Handlers {

  /**
   * Called on every token in valid open stacks. Use the {@link Token} instance to access the
   * current token.
   */
  callback: () => void;

  /**
   * A stack is being opened. Return false if you'd like to skip it and its close.
   */
  open: (stack: StackValues) => boolean|void;

  /**
   * A previously opened stack is being closed.
   */
  close: (stack: StackValues) => void;

}


/**
 * An interface to the current token. This will change what it is pointing to, when the parser
 * moves its head as it just reflects the current token.
 */
export interface Token {

  /**
   * The location in the code immediately after the previous token. Between the void and at
   * locations, it's possible be empty, whitespace only, or contain many comments.
   */
  void(): number;

  /**
   * The starting point of the current token.
   */
  at(): number;

  /**
   * The length of this token. It is possible for this to be zero in some rare cases.
   */
  length(): number;

  /**
   * The line number of this token (not the void).
   */
  lineNo(): number;

  /**
   * The type of this token.
   */
  type(): TokenValues;

  /**
   * Special for this token. This changes depending on token type. For ops and keywords, this
   * returns their hash. For close, it returns the type of its paired open token. It can return
   * other values from specials for other token types.
   */
  special(): number;

  /**
   * Finds the current subarray for this token, based on its location and length.
   */
  view(): Uint8Array;

  /**
   * Decodes as UTF-8 to a raw string the current subarray for this token.
   */
  string(): string;

  /**
   * Evaluates the current string token into a JS string (i.e., removes quotes and escapes). Throws
   * if pointing to a non-string, or a template string with holes.
   */
  stringValue(): string;
}

export interface Base {

  /**
   * Helpers to access the current cursor on callback.
   */
  token: Token;

  /**
   * Runs the parser over the entire source. Clears handlers on finish.
   *
   * @returns number of top-level statements
   */
  run(): number;

  /**
   * Replaces any number of handlers with passed handlers.
   * 
   * @param handlers to replace with
   */
  handle(handlers: Partial<Handlers>): void;

}

export interface Harness extends Base {

  /**
   * Prepares the parser for parsing. Returns storage where source should be written.
   *
   * @param size number of bytes needed
   * @returns storage to write to
   */
  prepare(size: number): Uint8Array;

}

export interface RewriterArgs {
  callback(): Uint8Array|string|void;
  stack(type: StackValues): boolean|void;
  write(part: Uint8Array): void;
}

export interface RewriterReturn {
  run(file: string, args?: Partial<RewriterArgs>): void;
  token: Token;
}


