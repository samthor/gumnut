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

#ifndef _STREAM_H
#define _STREAM_H

#define __MAX_PENDING_COLON 16

typedef struct {
  uint8_t reok : 1;  // would a regexp be ok here
  uint8_t initial : 1;  // only for TOKEN_BRACE
  uint8_t pending_function : 1;  // is there a pending function
  uint8_t pending_hoist_brace : 1;  // is there a pending top-level hoist brace (function, class)
  uint8_t pending_colon : 4;  // number of pending :'s after ?
} streamstack;

#endif//_STREAM_H