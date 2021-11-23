
#include "../token.h"
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

#include "read.c"

static int depth = 0;
static struct token *t;

static const char *token_names[] = {
  "eof",
};

int main() {
  char *buf;
  int len = read_stdin(&buf);
  if (len < 0) {
    return -1;
  }

  int ret = gumnut_init(buf, len);
  if (ret) {
    return ret;
  }

  struct token_t *cursor = gumnut_cursor();

  for (;;) {
    int ret = gumnut_next();
    if (ret < 0) {
      fprintf(stderr, "!! err=%d\n", ret);
      return ret;
    } else if (ret == 0) {
      break;
    }
    fprintf(stderr, "t=%d\ts=%.*s\n", ret, cursor->len, cursor->p);
  }

  fprintf(stderr, "done\n");
  return 0;
}
