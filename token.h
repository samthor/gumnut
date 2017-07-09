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
  int type;
  int line_no;
} token;

int prsr_next_token(tokendef *d, int slash_is_op, token *out);
tokendef prsr_init(char *p);

// empty: will not contain text
#define TOKEN_EOF       0

// fixed: will always be the same, or in the same set
#define TOKEN_SEMICOLON 1   // might be blank for ASI
#define TOKEN_NEWLINE   2
#define TOKEN_COMMA     3
#define TOKEN_SPREAD    4
#define TOKEN_DOT       5
#define TOKEN_OP        6   // can include 'in', 'instanceof'
#define TOKEN_ARROW     7
// nb. used to have     8
#define TOKEN_COLON     9
#define TOKEN_TERNARY   10
#define TOKEN_BRACE     11
#define TOKEN_ARRAY     12
#define TOKEN_PAREN     13

// variable: could be anything
#define TOKEN_COMMENT   14
#define TOKEN_STRING    15
#define TOKEN_REGEXP    16
#define TOKEN_NUMBER    17
#define TOKEN_SYMBOL    18
#define TOKEN_KEYWORD   19
#define TOKEN_LABEL     20

// internal use only
#define TOKEN_LIT       21

#endif//_TOKEN_H
