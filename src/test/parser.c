
#include "../token.h"
#include "../parser.h"
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>

typedef struct _testdef {
  const char *name;
  const char *input;
  int *expected;  // zero-terminated token types
  int is_module;
  struct testdef *next;  // for failures
} testdef;

static token *t;

struct {
  testdef *def;
  int at;
  int len;
  int error;
} active;

void modp_callback(int special) {
  int actual = t->type;
  int expected = -1;

  if (active.at < active.len) {
    expected = active.def->expected[active.at];
  } else if (active.at == active.len) {
    expected = 0;
  }

  if (actual != expected) {
    printf("%d: actual=%d expected=%d `%.*s`\n", active.at, actual, expected, t->len, t->p);
    active.error = 1;
  } else {
    printf("%d: ok=%d `%.*s`\n", active.at, actual, t->len, t->p);
  }
  ++active.at;
}

void modp_stack(int special) {
  // ignored
}

int run_testdef(testdef *def) {
  t = modp_token();

  active.def = def;
  active.at = 0;
  active.len = 0;
  active.error = 0;

  // count expected size
  while (def->expected[active.len]) {
    ++active.len;
  }

  printf(">> %s\n", def->name);

  int ret = modp_init((char *) def->input, 0);
  if (ret >= 0) {
    do {
      ret = modp_run();
    } while (ret > 0);
  }

  if (ret) {
    printf("ERROR: internal error (%d)\n", ret);
    return ret;
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
  testdef tdef; \
  tdef.name = _name; \
  tdef.input = _input; \
  tdef.is_module = _name[0] == '^'; \
  tdef.next = NULL; \
  int v[] = {__VA_ARGS__ TOKEN_EOF}; \
  tdef.expected = v; \
  int lerr = run_testdef(&tdef); \
  if (lerr) { \
    err |= lerr; \
    last->next = malloc(sizeof(testdef)); \
    last = (testdef *) last->next; \
    *last = tdef; \
    ++ecount; \
  } \
  printf("\n"); \
  ++count; \
}

int main() {
  int err = 0;
  int count = 0;
  int ecount = 0;
  testdef fail, *last = &fail;
  bzero(&fail, sizeof(fail));

  _test("zero", "\n");

  _test("single symbol", "foo",
    TOKEN_SYMBOL,    // foo
  );

  _test("simple", "var x = 1;",
    TOKEN_KEYWORD,    // var
    TOKEN_SYMBOL,    // x
    TOKEN_OP,        // =
    TOKEN_NUMBER,    // 1
    TOKEN_SEMICOLON, // ;
  );

  _test("ternary", "a ? : x\nfoo:\n?:",
    TOKEN_SYMBOL,    // a
    TOKEN_TERNARY,   // ?
    TOKEN_CLOSE,     // :
    TOKEN_SYMBOL,    // x
    TOKEN_LABEL,     // foo
    TOKEN_COLON,     // :
    TOKEN_TERNARY,   // ?
    TOKEN_CLOSE,     // :
  );

  _test("let is always keyword in strict", "+let",
    TOKEN_OP,        // +
    TOKEN_KEYWORD,   // let
  );

  _test("always prefer keyword", "x = if (a) /123/",
    TOKEN_SYMBOL,    // x
    TOKEN_OP,        // =
    TOKEN_KEYWORD,   // if
    TOKEN_PAREN,     // (
    TOKEN_SYMBOL,    // a
    TOKEN_CLOSE,     // )
    TOKEN_REGEXP,    // /123/
  );

  _test("control keyword starts new statement on newline", "x =\n if (a) /123/",
    TOKEN_SYMBOL,    // x
    TOKEN_OP,        // =
    TOKEN_KEYWORD,   // if
    TOKEN_PAREN,     // (
    TOKEN_SYMBOL,    // a
    TOKEN_CLOSE,     // )
    TOKEN_REGEXP,    // /123/
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
  );

  _test("class decl regexp", "class {} / 100 /",
    TOKEN_KEYWORD,   // class
    TOKEN_SYMBOL,    // empty symbol
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_REGEXP,    // / 100 /
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
  );

  _test("simple async arrow function", "async () => await /123/",
    TOKEN_KEYWORD,   // async
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_OP,        // =>
    TOKEN_OP,        // await
    TOKEN_REGEXP,    // /123/
  );

  _test("async arrow function", "() => async () => await\n/123/\nawait /1/",
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_OP,        // =>
    TOKEN_KEYWORD,   // async
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_OP,        // =>
    TOKEN_OP,        // await
    TOKEN_REGEXP,    // /123/
    TOKEN_KEYWORD,   // await
    TOKEN_REGEXP,    // /1/
  );

  _test("class statement", "x = class Foo extends {} { if(x) {} } /123/",
    TOKEN_SYMBOL,    // x
    TOKEN_OP,        // =
    TOKEN_KEYWORD,   // class
    TOKEN_LIT,       // Foo
    TOKEN_KEYWORD,   // extends
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_BRACE,     // {
    TOKEN_LIT,       // if
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

  _test("dict string name", "+{'abc'() {}}",
    TOKEN_OP,        // +
    TOKEN_BRACE,     // {
    TOKEN_STRING,    // 'abc'
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_CLOSE,     // }
  );

  _test("dict after comma", ",{}",
    TOKEN_OP,        // ,
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
  );

  // TODO: we know this but don't report it
  _test("ASI for PostfixExpression", "a\n++\nb",
    TOKEN_SYMBOL,    // a
    TOKEN_OP,        // ++
    TOKEN_SYMBOL,    // b
  );

  _test("dict keyword-ness", "void {async * get get() {}}",
    TOKEN_OP,        // void
    TOKEN_BRACE,     // {
    TOKEN_KEYWORD,   // async
    TOKEN_OP,        // *
    TOKEN_KEYWORD,   // get
    TOKEN_LIT,       // get
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_CLOSE,     // }
  );

  _test("yield is op", "function*() { yield /123/ }",
    TOKEN_KEYWORD,   // function
    TOKEN_OP,        // *
    TOKEN_SYMBOL,    //
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_OP,        // yield
    TOKEN_REGEXP,    // /123/
    TOKEN_CLOSE,     // }
  );

  _test("ASI rule for yield is ignored in group", "function*() { (yield\n/123/) }",
    TOKEN_KEYWORD,   // function
    TOKEN_OP,        // *
    TOKEN_SYMBOL,    //
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_PAREN,     // (
    TOKEN_OP,        // yield
    TOKEN_REGEXP,    // /123/
    TOKEN_CLOSE,     // )
    TOKEN_CLOSE,     // }
  );

  // TODO: In strict mode, let is always a keyword.
  _test("for() matches let keyword", "for(let x;let;);",
    TOKEN_KEYWORD,   // for
    TOKEN_PAREN,     // (
    TOKEN_KEYWORD,   // let
    TOKEN_SYMBOL,    // x
    TOKEN_SEMICOLON, // ;
    TOKEN_KEYWORD,   // let
    TOKEN_SEMICOLON, // ;
    TOKEN_CLOSE,     // )
    TOKEN_SEMICOLON, // ;
  );

  _test("for await() matches keyword", "for await(let x);",
    TOKEN_KEYWORD,   // for
    TOKEN_KEYWORD,   // await
    TOKEN_PAREN,     // (
    TOKEN_KEYWORD,   // let
    TOKEN_SYMBOL,    // x
    TOKEN_CLOSE,     // )
    TOKEN_SEMICOLON, // ;
  );

  _test("for(blah of foo) matches keyword", "for(const x of bar);",
    TOKEN_KEYWORD,   // for
    TOKEN_PAREN,     // (
    TOKEN_KEYWORD,   // const
    TOKEN_SYMBOL,    // x
    TOKEN_OP,        // of
    TOKEN_SYMBOL,    // bar
    TOKEN_CLOSE,     // )
    TOKEN_SEMICOLON, // ;
  );

  // nb. last semi is needed, attached to "while(0)" on its own
  _test("do-while while sanity check", "do while(2) x\nwhile(1) while(0);",
    TOKEN_KEYWORD,   // do
    TOKEN_KEYWORD,   // while
    TOKEN_PAREN,     // (
    TOKEN_NUMBER,    // 2
    TOKEN_CLOSE,     // )
    TOKEN_SYMBOL,    // x
    TOKEN_KEYWORD,   // while
    TOKEN_PAREN,     // (
    TOKEN_NUMBER,    // 1
    TOKEN_CLOSE,     // )
    TOKEN_KEYWORD,   // while
    TOKEN_PAREN,     // (
    TOKEN_NUMBER,    // 0
    TOKEN_CLOSE,     // )
    TOKEN_SEMICOLON, // ;
  );

  _test("do-while inside control", "if do ; while(0) bar",
    TOKEN_KEYWORD,   // if
    TOKEN_KEYWORD,   // do
    TOKEN_SEMICOLON, // ;
    TOKEN_KEYWORD,   // while
    TOKEN_PAREN,     // (
    TOKEN_NUMBER,    // 0
    TOKEN_CLOSE,     // )
    TOKEN_SYMBOL,    // bar
  );

  _test("do-while block", "do {} while ();",
    TOKEN_KEYWORD,   // do
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_KEYWORD,   // while
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_SEMICOLON, // ;
  );

  _test("do-while ASIs", "do foo\nwhile(0)",
    TOKEN_KEYWORD,   // do
    TOKEN_SYMBOL,    // foo
    TOKEN_KEYWORD,   // while
    TOKEN_PAREN,     // (
    TOKEN_NUMBER,    // 0
    TOKEN_CLOSE,     // )
  );

  _test("do-while stack", "do;while()bar",
    TOKEN_KEYWORD,   // do
    TOKEN_SEMICOLON, // ;
    TOKEN_KEYWORD,   // while
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_SYMBOL,    // bar
  );

  _test("do-while value-like", "do;while()\n/foo/",
    TOKEN_KEYWORD,   // do
    TOKEN_SEMICOLON, // ;
    TOKEN_KEYWORD,   // while
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_REGEXP,    // /foo/
  );

  _test("strict", "'use strict'; let",
    TOKEN_STRING,    // 'blah'
    TOKEN_SEMICOLON, // ;
    TOKEN_KEYWORD,   // let
  );

  _test("arrow ASI bug", "{_ => {}}",
    TOKEN_BRACE,     // {
    TOKEN_SYMBOL,    // _
    TOKEN_OP,        // =>
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_CLOSE,     // }
  );

  _test("arrow value bug", "{_ => {}/123/g;}",
    TOKEN_BRACE,     // {
    TOKEN_SYMBOL,    // _
    TOKEN_OP,        // =>
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_REGEXP,    // /123/g
    TOKEN_SEMICOLON, // ;
    TOKEN_CLOSE,     // }
  );

  _test("ASI in case", "switch { default: }",
    TOKEN_KEYWORD,   // switch
    TOKEN_BRACE,     // {
    TOKEN_KEYWORD,   // default
    TOKEN_COLON,     // :
    TOKEN_CLOSE,     // }
  );

  _test("dict method after colon", "void {:,get x() {}}",
    TOKEN_OP,        // void
    TOKEN_BRACE,     // {
    TOKEN_COLON,     // :
    TOKEN_OP,        // ,
    TOKEN_KEYWORD,   // get
    TOKEN_LIT,       // x
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_CLOSE,     // }
  );

  _test("dict closed on right", "+{x:}",
    TOKEN_OP,        // +
    TOKEN_BRACE,     // {
    TOKEN_LIT,       // x
    TOKEN_COLON,     // :
    TOKEN_CLOSE,     // }
  );

  _test("dict method", "void {[] () {}}",
    TOKEN_OP,        // void
    TOKEN_BRACE,     // {
    TOKEN_ARRAY,     // [
    TOKEN_CLOSE,     // ]
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_CLOSE,     // }
  );

  _test("check import", "import foo, {zing as what} from 'blah' /foo/",
    TOKEN_KEYWORD,   // import
    TOKEN_SYMBOL,    // foo
    TOKEN_OP,        // ,
    TOKEN_BRACE,     // {
    TOKEN_LIT,       // zing
    TOKEN_KEYWORD,   // as
    TOKEN_SYMBOL,    // what
    TOKEN_CLOSE,     // }
    TOKEN_KEYWORD,   // from
    TOKEN_STRING,    // 'blah'
    TOKEN_REGEXP,    // /foo/
  );

  _test("static", "class X { static x() {} }",
    TOKEN_KEYWORD,   // class
    TOKEN_SYMBOL,    // X
    TOKEN_BRACE,     // {
    TOKEN_KEYWORD,   // static
    TOKEN_LIT,       // x
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_CLOSE,     // }
  );

  _test("async assumed with dot", ".async()",
    TOKEN_OP,        // .
    TOKEN_LIT,       // async
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
  );

  // nb. invalid, but nothing else goes here
  _test("using await as name inside async", "async () => class await {}",
    TOKEN_KEYWORD,   // async
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_OP,        // =>
    TOKEN_KEYWORD,   // class
    TOKEN_LIT,       // await
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
  );

  _test("escape string", "'foo\\'bar'",
    TOKEN_STRING,    // 'foo\'bar'
  );

  _test("doesn't consume label", "{break}",
    TOKEN_BRACE,     // {
    TOKEN_KEYWORD,   // break
    TOKEN_CLOSE,     // }
  );

  _test("host function stops statement", "abc\nfunction foo() {}",
    TOKEN_SYMBOL,    // abc
    TOKEN_KEYWORD,   // function
    TOKEN_SYMBOL,    // foo
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
  );

  _test("async part of function", "async\nfunction\nfoo() {}",
    TOKEN_KEYWORD,   // async
    TOKEN_KEYWORD,   // function
    TOKEN_SYMBOL,    // foo
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
  );

  _test("await should treat ~ as unary op", "await ~123",
    TOKEN_KEYWORD,   // await
    TOKEN_OP,        // ~
    TOKEN_NUMBER,    // 123
  );

  _test("await should treat + as regular op", "await +123",
    TOKEN_KEYWORD,   // await
    TOKEN_OP,        // +
    TOKEN_NUMBER,    // 123
  );

  _test("hashbang as comment", "#!hello",
    TOKEN_COMMENT,   // #!hello
  );

  _test("hashbang as comment", "#!hello /*\nfoo",
    TOKEN_COMMENT,   // #!hello
    TOKEN_SYMBOL,    // foo
  );

  _test("hashbang with following comment", "#!hello\n//foo",
    TOKEN_COMMENT,   // #!hello
    TOKEN_COMMENT,   // //foo
  );

  _test("class extends op-like", "class X extends foo.bar { if() {} }",
    TOKEN_KEYWORD,   // class
    TOKEN_SYMBOL,    // X
    TOKEN_KEYWORD,   // extends
    TOKEN_SYMBOL,    // foo
    TOKEN_OP,        // .
    TOKEN_LIT,       // bar
    TOKEN_BRACE,     // {
    TOKEN_LIT,       // if
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_CLOSE,     // }
  );

  _test("ternary has no value", "?:/foo/",
    TOKEN_TERNARY,   // ?
    TOKEN_CLOSE,     // :
    TOKEN_REGEXP,    // /foo/
  );

  _test("control with trailing statement", "if foo\nbar",
    TOKEN_KEYWORD,   // if
    TOKEN_SYMBOL,    // foo
    TOKEN_SYMBOL,    // bar
  );

  _test("attach statements", "if()try{}finally{}",
    TOKEN_KEYWORD,   // if
    TOKEN_PAREN,     // (
    TOKEN_CLOSE,     // )
    TOKEN_KEYWORD,   // try
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
    TOKEN_KEYWORD,   // finally
    TOKEN_BRACE,     // {
    TOKEN_CLOSE,     // }
  );

  _test("solo async", "async(a)",
    TOKEN_SYMBOL,    // async
    TOKEN_PAREN,     // (
    TOKEN_SYMBOL,    // a
    TOKEN_CLOSE,     // )
  );

  _test("label inside block", "if foo: 1",
    TOKEN_KEYWORD,   // if
    TOKEN_LABEL,     // foo
    TOKEN_COLON,     // :
    TOKEN_NUMBER,    // 1
  );

  _test("return dict", "return {foo: foo}",
    TOKEN_KEYWORD,   // return
    TOKEN_BRACE,     // {
    TOKEN_LIT,       // foo
    TOKEN_COLON,     // :
    TOKEN_SYMBOL,    // foo
    TOKEN_CLOSE,     // }
  );

  _test("regexp as start of block", "{/f/}",
    TOKEN_BRACE,     // {
    TOKEN_REGEXP,    // /f/
    TOKEN_CLOSE,     // }
  );

  _test("orphaned keyword", "enum foo",
    TOKEN_KEYWORD,   // enum
    TOKEN_SYMBOL,    // foo
  );

  // restate all errors
  testdef *p = &fail;
  if (ecount) {
    printf("errors (%d/%d):\n", ecount, count);
  }
  while ((p = (testdef *) p->next)) {
    printf("-- %s\n", p->name);
  }
  return err;
}
