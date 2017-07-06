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

  return ok;
}