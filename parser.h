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

#include "token.h"

#define ERROR__EXPECT_ZERO -1
#define ERROR__SYNTAX      -3
#define ERROR__UNEXPECTED  -4
#define ERROR__STACK       -5
#define ERROR__TODO        -6
#define ERROR__INTERNAL    -9

#define __STACK_SIZE 512

typedef struct {
  uint8_t state;
  uint8_t flag;
} parserstack;

typedef struct {
  tokendef td;
  token prev, after_asi, after_restrict_semicolon;
  uint8_t flag;
  parserstack stack[__STACK_SIZE];
  parserstack *curr;
} parserdef;

int prsr_parser_init(parserdef *p, char *buf);
int prsr_next(parserdef *p, token *out);
int prsr_fp(char *buf, int (*fp)(token *));
