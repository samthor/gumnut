[![Tests](https://github.com/samthor/blep/workflows/Tests/badge.svg)](https://github.com/samthor/blep/actions)

# B L EcmaScript Parser


### Notes

* token always writes its next token to its internal storage: this helps us fast-path checking it


### Memory

We'll require O(2) memory: one for source code, and one for working space.
(This could be the same memory and we could be destructive to the source.)
The working space can be _larger_ than the space required for the source.
Web Assembly, our primary target, supports at least 2gb, so files can be ~1gb.

The working space has marks/updates placed at the corresponding location to the source to record the state of previous tricky questions (destructuring assignment, arrow functions).

We also use space up to our successfully parsed high water mark for temporary token storage.
TODO: but we can't guarantee that we can just yield all these tokens (end up in random state)
