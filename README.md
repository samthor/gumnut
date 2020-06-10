A fast, permissive JavaScript tokenizer and parser in C.
Assumes code written in "module mode", and should support all ECMAScript language features in the [draft specification](https://github.com/tc39/proposals/blob/master/finished-proposals.md) (as of May 2020).

This code is intended for rewriting tasks, and can be compiled via Web Assembly for the web (or Node).
It does not generate an AST as part of its work.

## Coverage

prsr correctly parses all 'pass' and 'pass-explicit' tests from [test262-parser-tests](https://github.com/tc39/test262-parser-tests), _except_ those which:

- rely on non-strict mode behavior (e.g., use variable names like `static` and `let`): modules run in strict mode by default
- use the `NULL` byte inside a string.

## Note

JavaScript has a single 'after-the-fact' ambiguity, as `async` is not a keyword—even in strict mode.
Here's an example:

```js
// this is a function call to a method "async"
async(/* anything can go here */) {}

// this is an async arrow function
async(/* anything can go here */) => {}
```

See [arrow functions break JavaScript parsers](https://dev.to/samthor/arrow-functions-break-javascript-parsers-1ldp) for more details.

### Implementation

The parser resolves this ambiguity, but must parse your code twice, or more in a pathological^ case.
And, for this parser to be useful as a bundler, non-async arrow functions also require this resolution: i.e., does `(` start an arrow function, or normal parens?

The slow expansion triggers where further arrow functions are found in the _arguments_ of an arrow function, e.g.:

```js
(a =
  (b =
    async (c =
      (final = () => {}) => {}
    ) => {}
  ) => {}
) => {}
```

Each ambigious arrow function will parse until it is found to be unambiguous.
Inner ambigious arrow functions found during this step will be resolved as part of the top-level pass.

<small>^This uses a bitmask, and will crash if you include more than 32 inner ambiguous arrow functions. Don't do this.</small>
