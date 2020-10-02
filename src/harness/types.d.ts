declare namespace blep {
  export interface Token {
    void(): number;
    at(): number;
    length(): number;
    lineNo(): number;
    type(): number;
    special(): number;
    view(): Uint8Array;
    string(): string;
  }

  export interface Base {

    /**
     * Helpers to access the current cursor on callback.
     */
    token: Token;

    /**
     * Runs the parser over the entire source.
     *
     * @param callback to call for tokens
     * @param stack handles stack calls, return true to allow stack
     */
    run(callback: () => void, stack?: (number) => boolean): void;

    /**
     * Pushes a new callback for tokens.
     * 
     * @param callback call for tokens
     */
    push(callback: () => void): void;

    /**
     * Pops the top callback for tokens, returning the new active callback. Cannot pop the original
     * callback configured in `run()`.
     * 
     * @returns the new active callback
     */
    pop(): () => void;
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
