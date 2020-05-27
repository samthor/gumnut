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
// this is a function call
async(/* anything can go here */) {}

// this is the value of an arrow function
async(/* anything can go here */) => {}
```

See [arrow functions break JavaScript parsers](https://dev.to/samthor/arrow-functions-break-javascript-parsers-1ldp) for more details.

In this case, prsr reports `async` as a `TOKEN_LIT`, which is an ambigious token.
You can change it after-the-fact if this is important to you.
