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

#ifndef _STREAM_H
#define _STREAM_H

#define __MAX_PENDING_COLON 8
#define __EXC_STACK_SIZE    64

typedef struct {
  uint8_t reok : 1;  // would a regexp be ok here
  uint8_t initial : 1;  // only for TOKEN_BRACE
  uint8_t is_brace : 1;  // for TOKEN_BRACE (and zero state)
  uint8_t is_dict : 1;  // is brace-like-dict
  uint8_t dict_left : 1;  // are we LHS of a dict
  uint8_t pending_function : 1;  // is there a pending function
  uint8_t pending_hoist_brace : 1;  // is there a pending top-level hoist brace (function, class)
  uint8_t pending_colon : 3;  // number of pending :'s after ?

  uint8_t dict_left_async : 2;  // we saw an 'async', could apply
  uint8_t pending_async : 1;
  uint8_t pending_generator : 1;

  uint8_t exc : 1;  // are we an execution scope (function)
} streamstack;

typedef struct {
  uint8_t arrow : 1;
  uint8_t async : 1;
  uint8_t generator : 1;
} streamexc;

typedef struct {
  unsigned int depth : __STACK_SIZE_BITS;
  uint8_t slash_is_op : 1;
  streamstack stack[__STACK_SIZE];

  uint8_t dexc : 6;
  streamexc exc[__EXC_STACK_SIZE];

  token prev;
} streamdef;

streamdef prsr_stream_init();
int prsr_stream_next(streamdef *sd, token *out);

#endif//_STREAM_H