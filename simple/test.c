
#include "../token.h"
#include "simple.h"
#include <stdio.h>

typedef struct {
  const char *name;
  const char *input;
  int *expected;  // zero-terminated token types
} testdef;

typedef struct {
  testdef *def;
  int at;
  int len;
  int error;
} testactive;

static void testdef_step(void *arg, token *t) {
  testactive *active = (testactive *) arg;

  int actual = t->type;
  int expected = -1;

  ++active->at;
  if (active->at < active->len) {
    expected = active->def->expected[active->at];
  } else if (active->at == active->len) {
    expected = 0;
  }

  if (actual != expected) {
    printf("%d: actual=%d expected=%d `%.*s`\n", active->at, actual, expected, t->len, t->p);
    active->error = 1;
  } else {
    printf("%d: ok=%d `%.*s`\n", active->at, actual, t->len, t->p);
  }
}

int run_testdef(testdef *def) {
  tokendef td = prsr_init_token((char *) def->input);
  testactive active = {
    .def = def,
    .at = -1,
    .len = 0,
    .error = 0,
  };

  // count expected size
  while (def->expected[active.len]) {
    ++active.len;
  }

  printf(">> %s\n", def->name);

  int out = prsr_simple(&td, testdef_step, &active);
  while (active.at + 1 < active.len) {
    token fake;
    fake.type = -1;
    testdef_step(&active, &fake);
  }

  if (out) {
    printf("ERROR: internal error (%d)\n", out);
    return out;
  } else if (active.at != active.len) {
    printf("ERROR: mismatched length, actual=%d expected=%d\n", active.at, active.len);
    return 1;
  } else if (active.error) {
    printf("ERROR\n");
    return active.error;
  }

  printf("OK!\n");
  return 0;
}

// defines a test for prsr: args must have a trailing comma
#define _test(_name, _input, ...) \
{ \
  testdef td; \
  td.name = _name; \
  td.input = _input; \
  int v[] = {__VA_ARGS__ TOKEN_EOF}; \
  td.expected = v; \
  ok |= run_testdef(&td); \
  printf("\n"); \
}

int main() {
  int ok = 0;

  _test("zero", "\n");

  _test("simple", "var x = 1;",
    TOKEN_KEYWORD,    // var
    TOKEN_SYMBOL,    // x
    TOKEN_OP,        // =
    TOKEN_NUMBER,    // 1
    TOKEN_SEMICOLON, // ;
  );

  _test("ternary", "a ? : :",
    TOKEN_SYMBOL,    // a
    TOKEN_TERNARY,   // ?
    TOKEN_CLOSE,     // :
    TOKEN_COLON,     // :
    TOKEN_SEMICOLON, // ASI ;
  );

  _test("let as symbol", "+let",
    TOKEN_OP,        // +
    TOKEN_SYMBOL,    // let
    TOKEN_SEMICOLON, // ASI ;
  );

  _test("invalid keyword use ignored", "x = if (a) /123/",
    TOKEN_SYMBOL,    // x
    TOKEN_OP,        // =
    TOKEN_KEYWORD,   // if
    TOKEN_PAREN,     // (
    TOKEN_SYMBOL,    // a
    TOKEN_CLOSE,     // )
    TOKEN_OP,        // /
    TOKEN_NUMBER,    // 123
    TOKEN_OP,        // /
    TOKEN_SEMICOLON, // ASI ;
  );

  _test("control keyword starts new statement on newline", "x =\n if (a) /123/",
    TOKEN_SYMBOL,    // x
    TOKEN_OP,        // =
    TOKEN_SEMICOLON, // ASI ;
    TOKEN_KEYWORD,   // if
    TOKEN_PAREN,     // (
    TOKEN_SYMBOL,    // a
    TOKEN_CLOSE,     // )
    TOKEN_REGEXP,    // /123/
    TOKEN_SEMICOLON, // ASI ;
  );

  _test("function decl regexp", "function foo(y) {} / 100 /",
    TOKEN_KEYWORD,   // function
    TOKEN_SYMBOL,    // foo
    TOKEN_PAREN,     // (
    TOKEN_SYMBOL,    // y
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_REGEXP,    // / 100 /
    TOKEN_SEMICOLON, // ASI ;
  );

  _test("function statement", "(function(y) {} / 100 /)",
    TOKEN_PAREN,     // (
    TOKEN_KEYWORD,   // function
    TOKEN_PAREN,     // (
    TOKEN_SYMBOL,    // y
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_OP,        // /
    TOKEN_NUMBER,    // 100
    TOKEN_OP,        // /
    TOKEN_CLOSE,     // )
    TOKEN_SEMICOLON, // ASI ;
  );

  _test("simple async arrow function", "async () => await /123/",
    TOKEN_LIT,       // async
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_ARROW,     // =>
    TOKEN_OP,        // await
    TOKEN_REGEXP,    // /123/
    TOKEN_SEMICOLON, // ASI ;
  );

  _test("async arrow function", "() => async () => await\n/123/\nawait /1/",
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_ARROW,     // =>
    TOKEN_LIT,       // async
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_ARROW,     // =>
    TOKEN_OP,        // await
    TOKEN_REGEXP,    // /123/
    TOKEN_SEMICOLON, // ASI ;
    TOKEN_SYMBOL,    // await
    TOKEN_OP,        // /
    TOKEN_NUMBER,    // 1
    TOKEN_OP,        // /
    TOKEN_SEMICOLON, // ASI ;
  );

  _test("class statement", "x = class Foo extends {} { if(x) {} } /123/",
    TOKEN_SYMBOL,    // x
    TOKEN_OP,        // =
    TOKEN_KEYWORD,   // class
    TOKEN_SYMBOL,    // Foo
    TOKEN_KEYWORD,   // extends
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_BRACE,     // {
    TOKEN_SYMBOL,    // if
    TOKEN_PAREN,     // (
    TOKEN_SYMBOL,    // x
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_CLOSE,     // }
    TOKEN_OP,        // /
    TOKEN_NUMBER,    // 123
    TOKEN_OP,        // /
  );

  _test("ASI for PostfixExpression", "a\n++\nb",
    TOKEN_SYMBOL,    // a
    TOKEN_SEMICOLON, // ASI ;
    TOKEN_OP,        // ++
    TOKEN_SYMBOL,    // b
    TOKEN_SEMICOLON, // ASI ;
  );

  _test("yield is symbol", "yield",
    TOKEN_SYMBOL,    // yield
    TOKEN_SEMICOLON, // ASI ;
  );

  _test("dict keyword-ness", "void {async * get get() {}}",
    TOKEN_OP,        // void
    TOKEN_BRACE,     // {
    TOKEN_KEYWORD,   // async
    TOKEN_OP,        // *
    TOKEN_KEYWORD,   // get
    TOKEN_SYMBOL,    // get
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_CLOSE,     // }
    TOKEN_SEMICOLON, // ASI ;
  );

  _test("yield is op", "function*() { yield /123/ }",
    TOKEN_KEYWORD,   // function
    TOKEN_OP,        // *
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_OP,        // yield
    TOKEN_REGEXP,    // /123/
    TOKEN_SEMICOLON, // ASI ;
    TOKEN_CLOSE,     // }
  );

  _test("yield is restricted", "function*() { yield\n/123/ }",
    TOKEN_KEYWORD,   // function
    TOKEN_OP,        // *
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_OP,        // yield
    TOKEN_SEMICOLON, // ASI ;
    TOKEN_REGEXP,    // /123/
    TOKEN_SEMICOLON, // ASI ;
    TOKEN_CLOSE,     // }
  );

  _test("ASI rule for yield is ignored in group", "function*() { (yield\n/123/) }",
    TOKEN_KEYWORD,   // function
    TOKEN_OP,        // *
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_PAREN,     // (
    TOKEN_OP,        // yield
    TOKEN_REGEXP,    // /123/
    TOKEN_CLOSE,     // )
    TOKEN_SEMICOLON, // ASI ;
    TOKEN_CLOSE,     // }
  );

  _test("for() matches let keyword", "for(let x;let;)",
    TOKEN_KEYWORD,   // for
    TOKEN_PAREN,     // (
    TOKEN_KEYWORD,   // let
    TOKEN_SYMBOL,    // x
    TOKEN_SEMICOLON, // ;
    TOKEN_SYMBOL,    // let
    TOKEN_SEMICOLON, // ;
    TOKEN_CLOSE,     // )
  );

  _test("for await() matches keyword", "for await(let x)",
    TOKEN_KEYWORD,   // for
    TOKEN_KEYWORD,   // await
    TOKEN_PAREN,     // (
    TOKEN_KEYWORD,   // let
    TOKEN_SYMBOL,    // x
    TOKEN_CLOSE,     // )
  );

  _test("for(blah of foo) matches keyword", "for(const x of bar)",
    TOKEN_KEYWORD,   // for
    TOKEN_PAREN,     // (
    TOKEN_KEYWORD,   // const
    TOKEN_SYMBOL,    // x
    TOKEN_KEYWORD,   // of
    TOKEN_SYMBOL,    // bar
    TOKEN_CLOSE,     // )
  );

  _test("strict mode await", "'use strict'; await x;",
    TOKEN_STRING,    // 'use strict';
    TOKEN_SEMICOLON, // ;
    TOKEN_KEYWORD,   // await
    TOKEN_SYMBOL,    // x
    TOKEN_SEMICOLON, // ;
  );

  return ok;
}