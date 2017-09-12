A JavaScript chunker (possibly a tokenizer) in C.
It's compilable to Web Assembly and has a demo in the `wasm` path.

This is not a polished or documented product.

## Stages

prsr consists of three component:

1. a raw JavaScript tokenizer, which understands UTF-8 bytes and generates tokens

2. a streamer, which deals with some of JS' nuances (mostly around `/` vs `/regexp/`) that can be fixed by moving forward

3. a parser, which identifies literals as symbols or keywords (and requires more memory/time) 

Each component includes its subordinate components.
As an end-user, you probably want to use the parser directly.
