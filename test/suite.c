#include "test.h"
#include "../token.h"

int main() {
  int ok = 0;

  _test("zero", "\n",
    TOKEN_NEWLINE,
  );

  _test("simple", "var x = 1;",
    TOKEN_KEYWORD,   // var
    TOKEN_SYMBOL,    // x
    TOKEN_OP,        // =
    TOKEN_NUMBER,    // 1
    TOKEN_SEMICOLON, // ;
  );

  // checks that we don't end with an ASI: not a statement
  _test("function hoist", "function foo() {}",
    TOKEN_KEYWORD,   // function
    TOKEN_SYMBOL,    // foo
    TOKEN_PAREN,     // (
    TOKEN_PAREN,     // )
    TOKEN_BRACE,     // {
    TOKEN_BRACE,     // }
  );

  _test("function hoist regexp", "function foo(y) {} / 100 /",
    TOKEN_KEYWORD,   // function
    TOKEN_SYMBOL,    // foo
    TOKEN_PAREN,     // (
    TOKEN_SYMBOL,    // y
    TOKEN_PAREN,     // )
    TOKEN_BRACE,     // {
    TOKEN_BRACE,     // }
    TOKEN_REGEXP,    // / 100 /
    TOKEN_SEMICOLON,
  );

  _test("function statement", "(function foo(y) {} / 100 /)",
    TOKEN_PAREN,     // (
    TOKEN_KEYWORD,   // function
    TOKEN_SYMBOL,    // foo
    TOKEN_PAREN,     // (
    TOKEN_SYMBOL,    // y
    TOKEN_PAREN,     // )
    TOKEN_BRACE,     // {
    TOKEN_BRACE,     // }
    TOKEN_OP,        // /
    TOKEN_NUMBER,    // 100
    TOKEN_OP,        // /
    TOKEN_PAREN,     // )
    TOKEN_SEMICOLON,
  );

  _test("function ASI", "const q = function bar() {x; y};",
    TOKEN_KEYWORD,   // const
    TOKEN_SYMBOL,    // q
    TOKEN_OP,        // =
    TOKEN_KEYWORD,   // function
    TOKEN_SYMBOL,    // bar
    TOKEN_PAREN,     // (
    TOKEN_PAREN,     // )
    TOKEN_BRACE,     // {
    TOKEN_SYMBOL,    // x
    TOKEN_SEMICOLON, // ;
    TOKEN_SYMBOL,    // y
    TOKEN_SEMICOLON,
    TOKEN_BRACE,     // }
    TOKEN_SEMICOLON, // ;
  );
  
  _test("ASI to ambiguate ++", "x\n++\ny",
    TOKEN_SYMBOL,    // x
    TOKEN_NEWLINE,
    TOKEN_SEMICOLON,
    TOKEN_OP,        // ++
    TOKEN_NEWLINE,
    TOKEN_SYMBOL,    // y
    TOKEN_SEMICOLON,
  );

  _test("do-while ASI", "do {} while (true)foo()",
    TOKEN_KEYWORD,   // do
    TOKEN_BRACE,     // {
    TOKEN_BRACE,     // }
    TOKEN_KEYWORD,   // while
    TOKEN_PAREN,     // (
    TOKEN_SYMBOL,    // true
    TOKEN_PAREN,     // )
    TOKEN_SEMICOLON,
    TOKEN_SYMBOL,    // foo
    TOKEN_PAREN,     // (
    TOKEN_PAREN,     // )
    TOKEN_SEMICOLON,
  );

  _test("class statement", "x = class Foo extends Bar { if(x) {} }",
    TOKEN_SYMBOL,    // x
    TOKEN_OP,        // =
    TOKEN_KEYWORD,   // class
    TOKEN_SYMBOL,    // Foo
    TOKEN_KEYWORD,   // extends
    TOKEN_SYMBOL,    // Bar
    TOKEN_BRACE,     // {
    TOKEN_SYMBOL,    // if
    TOKEN_PAREN,     // (
    TOKEN_SYMBOL,    // x
    TOKEN_PAREN,     // )
    TOKEN_BRACE,     // {
    TOKEN_BRACE,     // }
    TOKEN_BRACE,     // }
    TOKEN_SEMICOLON,
  );

  return ok;
}