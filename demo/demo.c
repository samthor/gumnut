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
#include "../stream.h"

// reads stdin into buf, reallocating as necessary. returns strlen(buf) or < 0 for error.
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
  char c = ' ';
  if (out->type == TOKEN_SEMICOLON && !out->len) {
    c = ';';
  } else if (out->invalid) {
    c = '!';
  }
  int len = out->len;
  if (out->type == TOKEN_COMMENT && out->p[len-1] == '\n') {
    --len;
  }
  printf("%c%4d: %.*s #%d\n", c, out->line_no, len, out->p, out->type);
  return 0;
}

int main() {
  printf("sizeof(tokendef)=%lu sizeof(streamdef)=%lu\n", sizeof(tokendef), sizeof(streamdef));

  char *buf;
  if (read_stdin(&buf) < 0) {
    return -1;
  }

  tokendef td = prsr_init_token(buf);
  streamdef sd = prsr_stream_init();

  int ret = 0;
  token out;
  do {
    // get next token
    ret = prsr_next_token(&td, &out, sd.slash_is_op);
    if (ret) {
      break;
    }

    // stream processor
    ret = prsr_stream_next(&sd, &out);
    if (ret) {
      break;
    }

    // render
    render(&out);
  } while (out.type);

  if (!out.type && out.invalid && !ret) {
    ret = 1;
  }
  return ret;
}
