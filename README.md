A fast, permissive JavaScript tokenizer and parser in C.

This is not a polished or documented product.
It's intended to be used with Web Assembly.

## Goals

This parser will not identify all problems with your code, but it will operate correctly on correct code.
It's ideal for a backend to a compiler or tool which might transform your code.

## Stages

1. a raw JavaScript tokenizer, which understands UTF-8 bytes and generates simple tokens

2. a simple parser, which deals with JS' nuances and outputs marked up tokens

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
Note that `async param => {}` is _not_ ambiguous, as it cannot be a function call.
