#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

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
  if (*out->p == '\n') {
    // don't display, javascript is dumb
  } else {
    printf("%c%4d: %.*s\n", out->after_whitespace ? '.' : ' ', out->line_no, out->len, out->p);
  }
  return 0;
}

int main() {
  char *buf;
  if (read_stdin(&buf) < 0) {
    return -1;
  }

  return prsr_consume(buf, render);
}