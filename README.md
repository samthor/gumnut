[![Tests](https://github.com/samthor/prsr/workflows/Tests/badge.svg)](https://github.com/samthor/prsr/actions)

A fast, permissive JavaScript tokenizer and parser in C.
Assumes code written in "module mode", and should support all ECMAScript language features in the [draft specification](https://github.com/tc39/proposals/blob/master/finished-proposals.md) (as of May 2020).

This code is intended for rewriting tasks, and can be compiled via Web Assembly for the web (or Node).
It's not reentrant, so you can't parse another file from within callbacks.
It does not generate an AST as part of its work, it does not modify the passed source code, and does not use `malloc`.

## Coverage

prsr correctly parses all 'pass' and 'pass-explicit' tests from [test262-parser-tests](https://github.com/tc39/test262-parser-tests), _except_ those which:

- rely on non-strict mode behavior (e.g., use variable names like `static` and `let`): modules run in strict mode by default
- use the `NULL` byte inside a string.

## Note

JavaScript has a single 'after-the-fact' ambiguity, as `async` is not a keyword—even in strict mode.
Here's an example:

```js
// this is a function call of a method named "async"
async(/* anything can go here */) {}

// this is an async arrow function
async(/* anything can go here */) => {}

// this calls the async method on foo, and is _not_ ambiguous
foo.async() {}
```

See [arrow functions break JavaScript parsers](https://dev.to/samthor/arrow-functions-break-javascript-parsers-1ldp) for more details.

### Solution

The parser resolves this ambiguity, but may parse your code twice, or more in a pathological^ case.
And, for this parser to be useful as a bundler, non-async arrow functions also require this resolution: i.e., does `(` start an arrow function, or normal parens?

Here's an extreme example:

```js
(
  a = (b =
        async (c =
          (final = () => {}) => {}
        )
      ) => {},
  foo(),
  x = () => {},
)
```

Here, `a` is an arrow function, `b` is a call to `async()`, and `c` plus `final` are both arrow functions.

The parser answers whether this question by parsing an outermost arrow function until it is found to be unambiguous (upon finding `foo()`, the outermost paren in the example is definitely not an arrow function).
As a side-effect, the first cluster of inner ambiguous arrows functions will be resolved and their status cached.

<small>^The parser uses a <code>uint32_t</code> to cache inner resolutions. Don't put more than 32 ambiguous arrow functions inside another.</small>
