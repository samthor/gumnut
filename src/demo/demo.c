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

static token *out;
static int tokens = 0;

void modp_callback(int special) {
  ++tokens;
#ifndef SPEED
  char c = ' ';
  if (out->hash) {
    c = '#';  // has a hash
  } else if (special) {
    if (special == SPECIAL__MODULE_PATH) {
      c = 'm';
    }
  }
  printf("%c%4d.%02d: %.*s\n", c, out->line_no, out->type, out->len, out->p);
#endif
}

int main() {
  char *buf;
  int len = read_stdin(&buf);
  if (len < 0) {
    return -1;
  }
  fprintf(stderr, ">> read %d bytes\n", len);

  out = modp_init(buf, 0);
  int ret;

  for (;;) {
    ret = modp_run();
    if (ret <= 0) {
      break;
    }
  }

  if (ret) {
    fprintf(stderr, "ret=%d\n", ret);
  }
  fprintf(stderr, ">> %d tokens\n", tokens);
  return ret;
}
