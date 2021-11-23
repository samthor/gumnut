#include "../core/token.h"
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

#include "read.c"

static int depth = 0;
static struct token *t;

static const char *stack_names[] = {
  "null",
  "expr",
  "declare",
  "control",
  "block",
  "function",
  "class",
  "misc",
  "label",
  "export",
  "module",
  "inner",
};

static const char *token_names[] = {
  "eof",
  "lit",
  "semicolon",
  "op",
  "colon",
  "brace",
  "array",
  "paren",
  "ternary",
  "close",
  "string",
  "regexp",
  "number",
  "symbol",
  "keyword",
  "label",
  "block",
};

void blep_parser_callback() {
  if (t->type < 0 || t->type > _TOKEN_MAX) {
    exit(1);
  }

  char hint = ' ';
  if (t->special & SPECIAL__LIT) {
    hint = '#';
  }

  printf("%-10s%c| ", token_names[t->type], hint);

  for (int i = 0; i < depth; ++i) {
    printf("  ");
  }

  printf("%.*s", t->len, t->p);

  if (t->special && !(t->special & SPECIAL__LIT) && t->type != TOKEN_CLOSE) {
    printf(" ~");
    if (t->special & SPECIAL__NEWLINE) {
      printf(" newline");
    }
    if (t->special & SPECIAL__DECLARE) {
      printf(" declare");
    }
    if (t->special & SPECIAL__TOP) {
      printf(" top");
    }
    if (t->special & SPECIAL__PROPERTY) {
      printf(" property");
    }
    if (t->special & SPECIAL__EXTERNAL) {
      printf(" external");
    }
    if (t->special & SPECIAL__CHANGE) {
      printf(" change");
    }
    if (t->special & SPECIAL__DESTRUCTURING) {
      printf(" destructuring");
    }
  }

  printf("\n");
}

int blep_parser_open(int type) {
  ++depth;
  if (type > _STACK_MAX) {
    exit(1);
  }
  printf("%-11s>\n", stack_names[type]);
  return 0;
}

void blep_parser_close(int type) {
  --depth;
  printf("%-11s<\n", stack_names[type]);
}

int main() {
  char *buf;
  int len = read_stdin(&buf);
  if (len < 0) {
    return -1;
  }

  int ret = blep_token_init(buf, len);
  if (ret) {
    return ret;
  }

  struct token *t = &(td->curr);

  for (;;) {
    ret = blep_token_next();
    if (ret < 0) {
      return ret;
    } else if (!ret) {
      break;  // no more tokens
    }

    char hint = ' ';

    printf("%-10s%c| ", token_names[t->type], hint);
    printf("%.*s\n", t->len, t->p);
  }

  return 0;
}
