A JavaScript tokenizer in C.
It's compilable to Web Assembly and has a demo in the `wasm` path.

This is not a polished or documented product.

## Stages

1. a raw JavaScript tokenizer, which understands UTF-8 bytes and generates simple tokens

2. stream code, which deals with JS nuances and outputs a correct^ stream of tokens

^JS has a fun ambiguity which is hard to overcome without a complete lookahead parser.
Without a lookahead step, it's not clear whether `async` below is a function call (calling the `async` function) or keyword (declaring an arrow function as `async`).

```js
// this is a function call
async(/*anything can go here*/) {}

// this is the value of an arrow function
async(/*anything can go here*/) => {}
```
