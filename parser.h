
typedef struct {
  char *p;
  int len;
  int after_whitespace;  // is there whitespace after this token?
  int line_no;
} token;

int prsr_consume(char *, int (*fp)(token *));
