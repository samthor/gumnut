/*
 * Copyright 2017 Sam Thorogood. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */

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
  NULL,  // was ELISON, now unused
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
  "LIT",
  NULL
};

int run_testdef(testdef *td) {
  parserdef d;
  prsr_parser_init(&d, (char *) td->input);

  token out;
  int ret;
  int i = 0;
  while (!(ret = prsr_next(&d, &out))) {
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

