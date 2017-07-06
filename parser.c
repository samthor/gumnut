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

#include <string.h>
#include <inttypes.h>
#include "token.h"
#include "utils.h"
#include "parser.h"

int prsr_token(char *buf, int (*fp)(token *)) {
  tokendef d;
  bzero(&d, sizeof(d));
  d.buf = buf;
  d.len = strlen(buf);
  d.depth = 1;
  d.line_no = 1;

  token out;
  int ret;
  while (!(ret = prsr_next_token(&d, &out))) {
    fp(&out);
  }
  return ret;
}
