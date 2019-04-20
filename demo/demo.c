/*
 * Copyright 2019 Sam Thorogood. All rights reserved.
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

typedef struct {
  int tokens;
  int asi;
} demo_context;

void render_callback(void *arg, token *out) {
  demo_context *context = (demo_context *) arg;
  ++context->tokens;
  if (out->type == TOKEN_SEMICOLON && !out->len) {
    ++context->asi;
  }
#ifdef SPEED
  // don't do anything, just show how fast this is?
#else
  char c = ' ';
  if (out->type == TOKEN_SEMICOLON && !out->len) {
    c = ';';  // this is an ASI
  }
  printf("%c%4d.%02d: %.*s (%d)\n", c, out->line_no, out->type, out->len, out->p, out->hash);
#endif
}

int main() {
  char *buf;
  int len = read_stdin(&buf);
  if (len < 0) {
    return -1;
  }
  fprintf(stderr, ">> read %d bytes\n", len);
  demo_context context;
  bzero(&context, sizeof(demo_context));

  tokendef td = prsr_init_token(buf);
  int out = prsr_simple(&td, 0, render_callback, &context);
  if (out) {
    fprintf(stderr, "ret=%d\n", out);
  }
  fprintf(stderr, ">> %d tokens (%d asi)\n", context.tokens, context.asi);
  return out;
}
