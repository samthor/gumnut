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

int isnum(char c) {
  return c >= '0' && c <= '9';
}

// nb. buf must contain words start/end with space, aka " test foo "
int in_space_string(const char *big, char *s, int len) {
  // TODO: do something better? strstr is probably fast D:
  // search for: space + candidate + space
  char cand[16];
  memcpy(cand+1, s, len);
  cand[0] = ' ';
  cand[len+1] = ' ';
  cand[len+2] = 0;

  return strstr(big, cand) != NULL;
}

int is_keyword(char *s, int len) {
  if (len > 10 || len < 2) {
    return 0;  // no statements <2 ('if' etc) or >10 ('implements')
  }
  for (int i = 0; i < len; ++i) {
    if (s[i] < 'a' || s[i] > 'z') {
      return 0;  // only a-z
    }
  }
  // nb. does not contain 'in' or 'instanceof', as they are ops
  // does not contain 'super', treated as symbol
  static const char v[] =
    " await break case catch class const continue debugger default delete do else enum export"
    " extends finally for function if implements import interface let new package private"
    " protected public return static switch throw try typeof var void while with yield ";
  return in_space_string(v, s, len);
}

int is_reserved_word(char *s, int len) {
  if (is_keyword(s, len)) {
    return 1;
  }
  if (len < 4 || len > 5) {
    return 0;
  }
  static const char v[] = " null true false ";
  return in_space_string(v, s, len);
}

// whether this is a control keyword that is not an expr
int is_control_keyword(char *s, int len) {
  if (len > 7 || len < 2) {
    return 0;  // no control <2 ('if' etc) or >5 ('finally' etc)
  }
  static const char v[] = " catch do if finally for switch try while with ";
  return in_space_string(v, s, len);
}

int is_asi_keyword(char *s, int len) {
  if (len > 9 || len < 5) {
    return 0;  // no asi <5 ('yield' etc) or >9 ('continue')
  }
  static const char v[] = " break continue return throw yield ";
  return in_space_string(v, s, len);
}

// keywords that may cause declarations (function is hoisted, class _technically_ isn't)
int is_hoist_keyword(char *s, int len) {
  return (len == 5 && !memcmp(s, "class", 5)) || (len == 8 && !memcmp(s, "function", 8));
}

// keywords that operate on something and return a value
//   e.g. 'void 1' returns undefined
//   yield is weird, it doesn't work as "yield + 1": needs "(yield) + 1"
int is_expr_keyword(char *s, int len) {
  if (len < 3 || len > 6) {
    return 0;
  }
  static const char v[] = " await delete new typeof void yield ";
  return in_space_string(v, s, len);
}

int is_labellike_keyword(char *s, int len) {
  return (len == 4 && !memcmp(s, "case", 4)) || (len == 7 && !memcmp(s, "default", 7));
}

int is_begin_expr_keyword(char *s, int len) {
  if (len < 3 || len > 6) {
    return 0;
  }
  static const char v[] = " var let const return throw ";
  return in_space_string(v, s, len);
}

int is_op_keyword(char *s, int len) {
  return (len == 2 || len == 10) &&
      !memcmp(s, "in", 2) && (len == 2 || !memcmp(s+2, "stanceof", 8));
}

int is_decl_keyword(char *s, int len) {
  return (len == 3 && (!memcmp(s, "var", 3) || !memcmp(s, "let", 3))) ||
      (len == 5 && !memcmp(s, "const", 5));
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