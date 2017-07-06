#include "test.h"
#include "../token.h"

int main() {
  int ok = 0;

  _test("simple", "var x = 1;",
    TOKEN_KEYWORD,   // var
    TOKEN_SYMBOL,    // x
    TOKEN_OP,        // =
    TOKEN_NUMBER,    // 1
    TOKEN_SEMICOLON, // ;
  );

  _test("function hoist", "function foo(y) {} / 100 /",
    TOKEN_KEYWORD,   // function
    TOKEN_SYMBOL,    // foo
    TOKEN_PAREN,     // (
    TOKEN_SYMBOL,    // y
    TOKEN_PAREN,     // )
    TOKEN_BRACE,     // {
    TOKEN_BRACE,     // }
    TOKEN_REGEXP,    // / 100 /
    TOKEN_SEMICOLON, // asi
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
    TOKEN_SEMICOLON, // asi
  );

  return ok;
}