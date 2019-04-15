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

#include <string.h>

#define MAX_LENGTH 10

static const char always_keyword[] =
    " async break case catch class const continue debugger default delete do else enum export"
    " extends finally for function if import new return static switch throw try typeof var void"
    " while with ";

static const char always_strict_keyword[] =
    " implements package protected interface private public ";

static int could_be_keyword(char *s, int len) {
  if (len > MAX_LENGTH || len < 2) {
    return 0;  // no statements <2 ('if' etc) or >10 ('implements')
  }
  for (int i = 0; i < len; ++i) {
    if (s[i] < 'a' || s[i] > 'z') {
      return 0;  // only a-z
    }
  }
  return 1;
}

// nb. buf must contain words start/end with space, aka " test foo "
int in_space_string(const char *big, char *s, int len) {
  // TODO: do something better? strstr is probably fast D:
  // search for: space + candidate + space
  if (len <= 0 || len > MAX_LENGTH) {
    return 0;
  }
  char cand[MAX_LENGTH + 2];
  memcpy(cand+1, s, len);
  cand[0] = ' ';
  cand[len+1] = ' ';
  cand[len+2] = 0;

  return strstr(big, cand) != NULL;
}

int is_always_keyword(char *s, int len, int strict) {
  if (!could_be_keyword(s, len)) {
    return 0;
  } else if (strict && in_space_string(always_strict_keyword, s, len)) {
    return 1;
  } else {
    // does not contain 'in' or 'instanceof', as they are ops
    // does not contain 'super' or 'this', treated as symbol
    return in_space_string(always_keyword, s, len);
  }
}

// nb. this is "is label safe"
int is_reserved_word(char *s, int len, int strict) {
  if (is_always_keyword(s, len, strict)) {
    return 1;
  }
  static const char v[] = " const false in instanceof null super this true var ";
  return in_space_string(v, s, len);
}

// whether this is a control keyword
int is_control_keyword(char *s, int len) {
  if (len > 7 || len < 2) {
    return 0;  // no control <2 ('if' etc) or >7 ('finally')
  }
  static const char v[] = " catch do else if finally for switch try while with ";
  return in_space_string(v, s, len);
}

// control group that likely has an immediate () after it
int is_control_paren(char *s, int len) {
  // nb. doesn't have "do", e.g., "do (100) / 100" is valid
  static const char v[] = " catch if for switch while with ";
  return in_space_string(v, s, len);
}

int is_decl_keyword(char *s, int len) {
  if (len < 3 || len > 5) {
    return 0;
  }
  static const char v[] = " var let const ";
  return in_space_string(v, s, len);
}

int is_op_keyword(char *s, int len) {
  return (len == 2 || len == 10) &&
      !memcmp(s, "in", 2) && (len == 2 || !memcmp(s+2, "stanceof", 8));
}

// keywords that may optionally have a label (and only a label) following them
int is_label_keyword(char *s, int len) {
  return (len == 5 && !memcmp(s, "break", 5)) || (len == 8 && !memcmp(s, "continue", 8));
}

// is ++ or --
int is_double_addsub(char *s, int len) {
  return len == 2 && (s[0] == '+' || s[0] == '-') && s[0] == s[1];
}

int is_getset(char *s, int len) {
  return len == 3 && (s[0] == 'g' || s[0] == 's') && !memcmp(s+1, "et", 2);
}

