
// TODO(samthor): This is a Node-specific type.
import * as stream from 'stream';

/**
 * Calls provided by the internal C code.
 */
export interface BlepCalls {
  __post_instantiate(): void;

  blep_parser_init(at: number, len: number): number;
  blep_parser_run(): number;
  blep_parser_cursor(): number;
}

/**
 * Imports required by the internal C code, including standard library calls.
 */
export interface BlepImports {
  memset(at: number, byte: number, size: number): void;
  memchr(at: number, byte: number, siez: number): number;

  blep_parser_callback(): void;
  blep_parser_open(type: number): 0 | 1;
  blep_parser_close(type: number): void;
}

export interface Handlers {
  callback: () => void;
  open: (number) => boolean|void;
  close: (number) => void;
}

/**
 * An interface to the current token. This will change what it is pointing to, when the parser
 * moves its head as it just reflects the current token.
 */
export interface Token {
  void(): number;
  at(): number;
  length(): number;
  lineNo(): number;
  type(): number;
  special(): number;

  /**
   * Finds the current subarray for this token.
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
  handle(handlers: Handlers): void;

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
  callback(): string|void;
  stack(type: number): boolean|void;
}

export interface RewriterReturn {
  run(file: string, args?: RewriterArgs): stream.Readable;
  token: Token;
}
