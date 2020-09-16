#include "token.h"
#include "parser.h"
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

#include "../prsr/src/demo/read.c"

static int depth = 0;

void blep_parser_callback(struct token *t) {
  for (int i = 0; i < depth; ++i) {
    printf("  ");
  }

  printf("%.*s (%d)\n", t->len, t->p, t->type);
}

int blep_parser_stack(int type) {
  if (type) {
    ++depth;
    printf(">> %d\n", type);
  } else {
    --depth;
    printf("<<\n");
  }
  return 0;
}

int main() {
  char *buf;
  int len = read_stdin(&buf);
  if (len < 0) {
    return -1;
  }
  fprintf(stderr, "!! read %d bytes\n", len);

  int ret = blep_token_init(buf, len);
  if (ret) {
    return ret;
  }

  blep_parser_init();
  for (;;) {
    int ret = blep_parser_run();
    if (ret <= 0) {
      fprintf(stderr, "!! err=%d\n", ret);
      return ret;
    }
  }
}