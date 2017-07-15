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

#define ERROR__UNEXPECTED  -1  // unavoidable syntax error
#define ERROR__STACK       -2  // stack overflow or underflow
#define ERROR__TODO        -3  // not yet implemented
#define ERROR__DUP         -4  // parser generated dup token
#define ERROR__INTERNAL    -5  // other internal error
#define ERROR__RETRY       -9  // internal: retry chunk_inner

#define __STACK_SIZE 472  // makes for ~1k parserdef

typedef struct {
  uint8_t state : 4;
  uint8_t flag : 4;
  uint8_t value;
} parserstack;

typedef struct {
  tokendef td;
  token prev, next;  // prev token is never COMMENT
  parserstack *curr;
  parserstack stack[__STACK_SIZE];
} parserdef;

int prsr_parser_init(parserdef *p, char *buf);
int prsr_next(parserdef *p, token *out);
int prsr_fp(char *buf, int (*fp)(token *));
