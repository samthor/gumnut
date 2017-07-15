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

#include <inttypes.h>

#ifndef _TOKEN_H
#define _TOKEN_H

typedef struct {
  char *buf;
  int curr;
  int len;
  int line_no;
} tokendef;

typedef struct {
  char *p;
  int len;
  int line_no;
  uint8_t type : 5;
  uint8_t invalid : 1;  // used by parser to indicate likely invalid
} token;

int prsr_next_token(tokendef *d, int slash_is_op, token *out);
tokendef prsr_init_token(char *p);

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

// variable: could be anything
#define TOKEN_COMMENT   12
#define TOKEN_STRING    13
#define TOKEN_REGEXP    14
#define TOKEN_NUMBER    15
#define TOKEN_SYMBOL    16
#define TOKEN_KEYWORD   17
#define TOKEN_LABEL     18

// literal: internal use except for reporting ambiguous tokens
#define TOKEN_LIT       19

#endif//_TOKEN_H
