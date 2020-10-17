declare namespace blep {
  export interface Handlers {
    callback?: () => void;
    open?: (number) => boolean|void;
    close?: (number) => void;
  }

  export interface Token {
    void(): number;
    at(): number;
    length(): number;
    lineNo(): number;
    type(): number;
    special(): number;
    view(): Uint8Array;
    string(): string;
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
    handle(handlers: blep.Handlers): void;

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
}
