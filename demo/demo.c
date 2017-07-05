#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../token.h"
#include "../parser.h"

// reads stdin into buf, reallocating as nessecary. returns strlen(buf) or < 0 for error.
int read_stdin(char **buf) {
  int pos = 0;
  int size = 1024;
  *buf = malloc(size);

  for (;;) {
    if (pos >= size - 1) {
      size *= 2;
      *buf = realloc(*buf, size);
    }
    if (!fgets(*buf + pos, size - pos, stdin)) {
      break;
    }
    pos += strlen(*buf + pos);
  }
  if (ferror(stdin)) {
    return -1;
  } 

  return pos;
}

int render(token *out) {
  if (out->len == 1 && *out->p == '\n') {
    // don't display, javascript is dumb
  } else if (out->type == TOKEN_ASI) {
    printf(";%4d: \n", out->line_no);
  } else {
    printf("%c%4d: %.*s #%d\n", out->whitespace_after ? '.' : ' ', out->line_no, out->len, out->p, out->type);
  }
  return 0;
}

int main() {
  char *buf;
  if (read_stdin(&buf) < 0) {
    return -1;
  }

  int rem = prsr_token(buf, render);
  if (rem > 0) {
    printf("can't parse reminder:\n%s\n", buf + rem);
    return -2;
  }

  return 0;
}
