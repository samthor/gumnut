
typedef struct {
  char *p;
  int len;
  int after_whitespace;  // is there whitespace after this token?
  int line_no;
  int type;
} token;

#define PRSR_TYPE_NEWLINE   1
#define PRSR_TYPE_COMMENT   2
#define PRSR_TYPE_STRING    3
#define PRSR_TYPE_SEMICOLON 4
#define PRSR_TYPE_REGEXP    5
#define PRSR_TYPE_NUMBER    6
#define PRSR_TYPE_DOTDOTDOT 7
#define PRSR_TYPE_DOT       8
#define PRSR_TYPE_OP        9
#define PRSR_TYPE_ARROW     10
#define PRSR_TYPE_KEYWORD   11
#define PRSR_TYPE_VAR       12
#define PRSR_TYPE_CONTROL   13
#define PRSR_TYPE_ARRAY     15
#define PRSR_TYPE_BRACKET   17
#define PRSR_TYPE_COMMA     19
#define PRSR_TYPE_COLON     20
#define PRSR_TYPE_TERNARY   21

int prsr_consume(char *, int (*fp)(token *));
