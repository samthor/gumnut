A fast, permissive JavaScript tokenizer and parser in C.

This is not a polished or documented product.
It's intended to be used with Web Assembly.
Check out a [live demo](https://t.co/jJuOG1lt7d)!

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
