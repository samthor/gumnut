#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../token.h"
#include "../parser.h"
#include "test.h"

// TODO: make this generate from token.h somehow
const char *pretty_types[] = {
  "EOF",
  "SEMICOLON",
  "NEWLINE",
  "COMMA",
  "SPREAD",
  "DOT",
  "OP",
  "ARROW",
  "ELISON",
  "COLON",
  "TERNARY",
  "BRACE",
  "ARRAY",
  "PAREN",
  "COMMENT",
  "STRING",
  "REGEXP",
  "NUMBER",
  "SYMBOL",
  "KEYWORD",
  "LABEL",
  NULL
};

int run_testdef(testdef *td) {
  tokendef d;
  bzero(&d, sizeof(d));
  d.buf = (char *) td->input;
  d.len = strlen(d.buf);
  d.depth = 1;
  d.line_no = 1;

  token out;
  int ret;
  int i = 0;
  while (!(ret = prsr_next_token(&d, &out))) {
    int expect = td->expected[i];
    if (!expect) {
      if (out.type != 0) {
        printf("%s: invalid length (expected=%d, was more)\n", td->name, i);
        return 1;
      }
      break;  // A-OK
    } else if (out.type == 0 && td->expected[i]) {
      printf("%s: invalid length (expected more, was %d)\n", td->name, i);
      return 2;
    } else if (expect != out.type) {
      printf("%s: expected token `%.*s` (%d) to be %s, was %s\n",
          td->name, out.len, out.p, i, pretty_types[expect], pretty_types[out.type]);
      return 3;
    }
    ++i;
  }
  if (ret) {
    printf("%s: couldn't parse input (%d)\n", td->name, ret);
    return -4;
  }
  printf("%s: ok\n", td->name);
  return 0;
}

