
#include <inttypes.h>

#ifndef _TOKEN_H
#define _TOKEN_H

#define _TOKEN_STACK_SIZE 224

typedef struct {
  char *buf;
  int curr;
  int len;
  int line_no;
  int prev_type;  // except comments and newlines
  uint8_t flags;
  uint8_t depth;  // must be >=1
  uint8_t stack[_TOKEN_STACK_SIZE];
} tokendef;

typedef struct {
  char *p;
  int len;
  int type;
  int whitespace_after;  // is there whitespace after this token?
  int line_no;
} token;

int prsr_next_token(tokendef *, token *);

// empty: will not contain text
#define TOKEN_EOF       0

// fixed: will always be the same, or in the same set
#define TOKEN_SEMICOLON 1   // might be blank for ASI
#define TOKEN_NEWLINE   2
#define TOKEN_COMMA     3
#define TOKEN_SPREAD    4
#define TOKEN_DOT       5
#define TOKEN_OP        6   // includes 'in', 'instanceof'
#define TOKEN_ARROW     7   // arrow function =>
#define TOKEN_ELISON    8
#define TOKEN_COLON     9
#define TOKEN_TERNARY   10
#define TOKEN_BRACE     11
#define TOKEN_ARRAY     12
#define TOKEN_PAREN     13

// variable: could be anything
#define TOKEN_COMMENT   14
#define TOKEN_STRING    15
#define TOKEN_REGEXP    16
#define TOKEN_NUMBER    17
#define TOKEN_SYMBOL    18
#define TOKEN_KEYWORD   19
#define TOKEN_LABEL     20

#endif//_TOKEN_H
