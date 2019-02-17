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

#ifndef _TYPES_H
#define _TYPES_H

#define ERROR__INTERNAL -1  // internal error

#define __STACK_SIZE      256  // stack size used by token
#define __STACK_SIZE_BITS 8    // bits needed for __STACK_SIZE

typedef struct {
  char *p;
  int len;
  int line_no;
  uint8_t type : 5;
  uint8_t lit_next_colon : 1;
} token;

// empty: will not contain text
#define TOKEN_EOF       0

// fixed: will always be the same, or in the same set
#define TOKEN_SEMICOLON 1   // might be blank for ASI
#define TOKEN_COMMA     2
#define TOKEN_SPREAD    3
#define TOKEN_DOT       4
#define TOKEN_OP        5   // can include 'in', 'instanceof'
#define TOKEN_ARROW     6
#define TOKEN_COLON     7
#define TOKEN_TERNARY   8
#define TOKEN_BRACE     9
#define TOKEN_ARRAY     10
#define TOKEN_PAREN     11
#define TOKEN_T_BRACE   12  // '${' within template literal
#define TOKEN_CLOSE     13  // '}', ']' or ')'

// variable: could be anything
#define TOKEN_COMMENT   14
#define TOKEN_STRING    15
#define TOKEN_REGEXP    16
#define TOKEN_NUMBER    17
#define TOKEN_SYMBOL    18
#define TOKEN_KEYWORD   19
#define TOKEN_LABEL     20  // to the left of a ':', e.g. 'foo:'

// literal: internal use except for reporting ambiguous tokens
#define TOKEN_LIT       21

#endif//_TYPES_H

