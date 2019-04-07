abc = {def:123};

x = abc ? 1 :
123;

x = ++
a;

console.info(x);

// implies read next line: op, dot, ternary, colon, ... (but doesn't matter, in paren)
// ... don't care what's on next line, but treat as part of us

// unhelpful: +/- can be joined to next thing, maybe drop?