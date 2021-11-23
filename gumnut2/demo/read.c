/*
 * Copyright 2019 Sam Thorogood.
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

#include <stdlib.h>

// reads stdin into buf, reallocating as necessary. returns strlen(buf) or < 0 for error.
int read_stdin(char **buf) {
  int pos = 0;
  int size = 1024;
  *buf = malloc(size);

  while (!feof(stdin)) {
    if (pos >= size - 1) {
      size *= 2;
      *buf = realloc(*buf, size);
    }

    size_t read = fread(*buf + pos, 1, size - pos, stdin);
    if (ferror(stdin)) {
      return -1;
    }
    pos += read;
  }

  return pos;
}
