
typedef struct {
  char *p;
  int len;
  int type;
  int whitespace_after;  // is there whitespace after this token?
  int line_no;
} token;

int prsr_token(char *, int (*fp)(token *));

// empty: will not containn text
#define TOKEN_EOF       0

// fixed: will always be the same, or in the same set
#define TOKEN_NEWLINE   1
#define TOKEN_SEMICOLON 2
#define TOKEN_SPREAD    3
#define TOKEN_DOT       4
#define TOKEN_OP        5  // includes 'in', 'instanceof'
#define TOKEN_ARROW     6
#define TOKEN_ELISON    7
#define TOKEN_COLON     8
#define TOKEN_TERNARY   9
#define TOKEN_BRACE     15
#define TOKEN_ARRAY     16
#define TOKEN_PAREN     17

// variable: could be anything
#define TOKEN_COMMENT   10
#define TOKEN_STRING    11
#define TOKEN_REGEXP    12
#define TOKEN_NUMBER    13
#define TOKEN_SYMBOL    14
#define TOKEN_KEYWORD   15

