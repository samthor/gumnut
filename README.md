A fast, permissive JavaScript tokenizer and parser in C.

One primary use case is to compile this to the web itself, with Web Assembly.
Check out a [live demo](https://t.co/jJuOG1lt7d)!

# Usage

As well as the live demo, you can try out `prsr` in your terminal (with Clang installed).
Try it out:

```bash
echo "var x = 123;" | ./demo/debug.sh
```

## Speed Tests

If you have a large JS file handy, you can pass it to `speed.sh` to check parse time:

```bash
cat large-js-file | ./demo/speed.sh
```

## Unit Tests

There's a small unit test suite in `./test/run.sh`.

# Remaining Tasks

* Distinguish between class declarations and dictionaries; `prsr` does not correctly identify e.g. "class { x = 123; }"
* Generate hints useful enough for a full AST; currently most useful as syntax highlighter

# Design

## Goals

This parser will not identify all problems with your code, but it will operate correctly on correct code.
It's ideal for a backend to a compiler or tool which might transform your code.

## Stages

1. a raw JavaScript [tokenizer](token.c), which understands UTF-8 bytes and generates simple tokens

2. a simple [parser](parser.c), which deals with JS' nuances and outputs marked up tokens

### Caveats

JavaScript has a single ambiguity which can only be resolved after-the-fact.
Here's an example:

```js
// this is a function call
async(/*anything can go here*/) {}

// this is the value of an arrow function
async(/*anything can go here*/) => {}
```

This parser announces what `async` is once it encouters an arrow function (or not).
There are two exceptions:

* single params canont be a function call, e.g., `async param => {}`
* use of `async` following a dot is always an object property or method, e.g. `this.async(arg)`
