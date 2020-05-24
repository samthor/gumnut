A fast, permissive JavaScript tokenizer and parser in C.
Assumes module mode ES5 and above.

This code is intended for rewriting tasks such as resolving module dependencies, and can be compiled via Web Assembly for the web (or Node).
It does not generate an AST.

## Note

JavaScript has a single 'after-the-fact' ambiguity.
Here's an example:

```js
// this is a function call
async(/*anything can go here*/) {}

// this is the value of an arrow function
async(/*anything can go here*/) => {}
```

