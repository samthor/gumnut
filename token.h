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
  int curr;
  int len;
  int line_no;
  uint8_t flag : 2;
  uint16_t depth : __STACK_SIZE_BITS;
  uint32_t stack[((__STACK_SIZE - 1) >> 5) + 1];
} tokendef;

typedef struct {
  int (*check)(void *);
  void *context;
} tokenvalue;

int prsr_next_token(tokendef *d, token *out, tokenvalue tv);
tokendef prsr_init_token(char *p);

#endif//_TOKEN_H
