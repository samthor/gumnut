#include "token.h"

#define _LOOKUP__OP_1      33  // 32 + 1
#define _LOOKUP__OP_2      34  // 32 + 2
#define _LOOKUP__OP_3      35  // 32 + 3
#define _LOOKUP__SLASH     36
#define _LOOKUP__DOT       37
#define _LOOKUP__Q         38
#define _LOOKUP__COMMA     39  // token with MISC_COMMA
#define _LOOKUP__NEWLINE   40
#define _LOOKUP__SPACE     41
#define _LOOKUP__STRING    42  // " or '
#define _LOOKUP__TEMPLATE  43  // `
#define _LOOKUP__LIT       44  // could be a hash
#define _LOOKUP__SYMBOL    45  // never hashed
#define _LOOKUP__NUMBER    46
#define _LOOKUP__SEMICOLON 47

static char lookup_symbol[256] = {
// 0-127
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 1, 0, 0, 0,  // just $
  0, 0, 0, 0, 0, 0, 0, 0,
  1, 1, 1, 1, 1, 1, 1, 1,  // 0-7
  1, 1, 0, 0, 0, 0, 0, 0,  // 8-9
  0, 1, 1, 1, 1, 1, 1, 1,  // A-G
  1, 1, 1, 1, 1, 1, 1, 1,  // H-O
  1, 1, 1, 1, 1, 1, 1, 1,  // P-W
  1, 1, 1, 0, 2, 0, 0, 1,  // X-Z, \ (special), _
  0, 1, 1, 1, 1, 1, 1, 1,  // a-g
  1, 1, 1, 1, 1, 1, 1, 1,  // h-o
  1, 1, 1, 1, 1, 1, 1, 1,  // p-w
  1, 1, 1, 0, 0, 0, 0, 0,  // x-z

// 128-255
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
};

static char lookup_op[256] = {
  TOKEN_EOF,  // 0, null
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  _LOOKUP__SPACE,  // 9, \t
  _LOOKUP__NEWLINE,  // 10, \n
  _LOOKUP__SPACE,
  _LOOKUP__SPACE,
  _LOOKUP__SPACE,  // 13, \r
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,  // 16
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,
  TOKEN_EOF,

  _LOOKUP__SPACE,  // 32, space
  _LOOKUP__OP_1,  // 33, !
  _LOOKUP__STRING,  // 34, "
  _LOOKUP__SYMBOL,  // 35, #
  _LOOKUP__SYMBOL,  // 36, $
  _LOOKUP__OP_1,  // 37, %
  _LOOKUP__OP_2,  // 38, &
  _LOOKUP__STRING,  // 39, '
  TOKEN_PAREN,  // 40, (
  TOKEN_CLOSE,  // 41, )
  _LOOKUP__OP_2,  // 42, *
  _LOOKUP__OP_1,  // 43, +
  _LOOKUP__COMMA,  // 44, ,
  _LOOKUP__OP_1,  // 45, -
  _LOOKUP__DOT,  // 46, .
  _LOOKUP__SLASH,  // 47, /

  _LOOKUP__NUMBER,  // 48, 0
  _LOOKUP__NUMBER,  // 49, 1
  _LOOKUP__NUMBER,  // 50, 2
  _LOOKUP__NUMBER,  // 51, 3
  _LOOKUP__NUMBER,  // 52, 4
  _LOOKUP__NUMBER,  // 53, 5
  _LOOKUP__NUMBER,  // 54, 6
  _LOOKUP__NUMBER,  // 55, 7
  _LOOKUP__NUMBER,  // 56, 8
  _LOOKUP__NUMBER,  // 57, 9

  TOKEN_COLON,  // 58, :
  _LOOKUP__SEMICOLON,  // 59, ;
  _LOOKUP__OP_2,  // 60, <
  _LOOKUP__OP_1,  // 61, =
  _LOOKUP__OP_3,  // 62, >
  _LOOKUP__Q,  // 63, ?
  TOKEN_EOF,  // 64, @

  _LOOKUP__SYMBOL, // 65, A
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,  // 90, Z

  TOKEN_ARRAY,  // 91, [
  _LOOKUP__SYMBOL,  // 92, forward slash "\"
  TOKEN_CLOSE,  // 93, ]
  _LOOKUP__OP_1,  // 94, ^
  _LOOKUP__SYMBOL,  // 95, _
  _LOOKUP__TEMPLATE,  // 96, '

  // nb. [h, j, k, m, q, x, z] don't start keywords
  _LOOKUP__LIT,  // 97, a
  _LOOKUP__LIT,  // 98, b
  _LOOKUP__LIT,  // 99, c
  _LOOKUP__LIT,  // 100, d
  _LOOKUP__LIT,  // 101, e
  _LOOKUP__LIT,  // 102, f
  _LOOKUP__LIT,  // 103, g
  _LOOKUP__SYMBOL,  // 104, h
  _LOOKUP__LIT,  // 105, i
  _LOOKUP__SYMBOL,  // 106, j
  _LOOKUP__SYMBOL,  // 107, k
  _LOOKUP__LIT,  // 108, l
  _LOOKUP__SYMBOL,  // 109, m
  _LOOKUP__LIT,  // 110, n
  _LOOKUP__LIT,  // 111, o
  _LOOKUP__LIT,  // 112, p
  _LOOKUP__SYMBOL,  // 113, q
  _LOOKUP__LIT,  // 114, r
  _LOOKUP__LIT,  // 115, s
  _LOOKUP__LIT,  // 116, t
  _LOOKUP__LIT,  // 117, u
  _LOOKUP__LIT,  // 118, v
  _LOOKUP__LIT,  // 119, w
  _LOOKUP__SYMBOL,  // 121, x
  _LOOKUP__LIT,  // 120, y
  _LOOKUP__SYMBOL,  // 122, z

  TOKEN_BRACE,  // 123, {
  _LOOKUP__OP_2,  // 124, |
  TOKEN_CLOSE,  // 125, }

  _LOOKUP__OP_1,  // 126, ~
  TOKEN_EOF,  // 127

// 128-255
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
};
