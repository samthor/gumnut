
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

  ++active->at;
  if (active->at >= active->len) {
    return;  // nothing to compare
  }

  int actual = t->type;
  int expected = active->def->expected[active->at];

  if (actual != expected) {
    printf("%d: actual=%d expected=%d `%.*s`\n", active->at, actual, expected, t->len, t->p);
    active->error = 1;
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
  if (out) {
    printf("internal error (%d)\n", out);
    return out;
  } else if (active.at != active.len) {
    printf("mismatched length, actual=%d expected=%d\n", active.at, active.len);
    return 1;
  } else if (active.error) {
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
    TOKEN_LIT,       // var
    TOKEN_LIT,       // x
    TOKEN_OP,        // =
    TOKEN_NUMBER,    // 1
    TOKEN_SEMICOLON, // ;
  );

  _test("ternary", "a ? : :",
    TOKEN_LIT,       // a
    TOKEN_TERNARY,   // ?
    TOKEN_CLOSE,     // :
    TOKEN_COLON,     // :
  );

  _test("function decl regexp", "function foo(y) {} / 100 /",
    TOKEN_LIT,       // function
    TOKEN_LIT,       // foo
    TOKEN_PAREN,     // (
    TOKEN_LIT,       // y
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_REGEXP,    // / 100 /
  );

  _test("function statement", "(function(y) {} / 100 /)",
    TOKEN_PAREN,     // (
    TOKEN_LIT,       // function
    TOKEN_PAREN,     // (
    TOKEN_LIT,       // y
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_OP,        // /
    TOKEN_NUMBER,    // 100
    TOKEN_OP,        // /
    TOKEN_CLOSE,     // )
  );

  _test("simple async arrow function", "async () => await /123/",
    TOKEN_LIT,       // async
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_ARROW,     // =>
    TOKEN_LIT,       // await
    TOKEN_REGEXP,    // /123/
  );

  _test("async arrow function", "() => async () => await /123/\nawait /1/",
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_ARROW,     // =>
    TOKEN_LIT,       // async
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_ARROW,     // =>
    TOKEN_LIT,       // await
    TOKEN_REGEXP,    // /123/
    TOKEN_LIT,       // await
    TOKEN_OP,        // /
    TOKEN_NUMBER,    // 1
    TOKEN_OP,        // /
  );

  _test("class statement", "x = class Foo extends {} { if(x) {} } /123/",
    TOKEN_LIT,       // x
    TOKEN_OP,        // =
    TOKEN_LIT,       // class
    TOKEN_LIT,       // Foo
    TOKEN_LIT,       // extends
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_BRACE,     // {
    TOKEN_LIT,       // if
    TOKEN_PAREN,     // (
    TOKEN_LIT,       // x
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_CLOSE,     // }
    TOKEN_OP,        // /
    TOKEN_NUMBER,    // 123
    TOKEN_OP,        // /
  );

  return ok;
}