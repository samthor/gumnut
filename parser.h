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

#define ERROR__VALUE_NO_EXPR -1  // internal error
#define ERROR__STACK         -2
#define ERROR__SYNTAX        -3

#define _TOKEN_STACK_SIZE 224

typedef struct {
  tokendef td;
  int prev_type;  // except comments and newlines
  uint8_t flags;
  uint8_t depth;  // must be >=1
  uint8_t stack[_TOKEN_STACK_SIZE];
  token pending_asi;
} parserdef;

int prsr_next(parserdef *p, token *out);
int prsr_fp(char *buf, int (*fp)(token *));
