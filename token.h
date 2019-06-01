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

#include <stdint.h>
#include "types.h"

#ifndef _TOKEN_H
#define _TOKEN_H

typedef struct {
  char *buf;
  int line_no;    // after next
  token next;     // next useful token
  token pending;  // pending comment
  int line_after_pending;

  // depth/flag used to record ${} state (resume literal once brace done)
  uint8_t flag : 2;
  uint16_t depth : __STACK_SIZE_BITS;
  uint8_t stack[__STACK_SIZE];
} tokendef;

int prsr_next_token(tokendef *d, token *out, int has_value);
void prsr_close_op_next(tokendef *d);
tokendef prsr_init_token(char *p);

#endif//_TOKEN_H
