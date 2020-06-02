/*
 * Copyright 2020 Sam Thorogood.
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

#include <ctype.h>
#include <string.h>
#include "helper.h"
#include "token.h"

#define FLAG__PENDING_T_BRACE 1
#define FLAG__RESUME_LIT      2

#define _LOOKUP__EOF      0
#define _LOOKUP__OP_1     1
#define _LOOKUP__OP_2     2
#define _LOOKUP__OP_3     3
#define _LOOKUP__SLASH    4
#define _LOOKUP__DOT      5
#define _LOOKUP__Q        6
#define _LOOKUP__COMMA    7  // token with MISC_COMMA
#define _LOOKUP__NEWLINE  8
#define _LOOKUP__SPACE    9
#define _LOOKUP__STRING   10  // " or '
#define _LOOKUP__TEMPLATE 11  // `
#define _LOOKUP__LIT      12  // could be a hash
#define _LOOKUP__SYMBOL   13  // always symbol, never hashed
#define _LOOKUP__NUMBER   14

#define _LOOKUP__TOKEN    32

static char lookup_symbol[256] = {
// 0-127
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 1, 0, 0, 0,  // just $
  0, 0, 0, 0, 0, 0, 0, 0,
  1, 1, 1, 1, 1, 1, 1, 1,  // 0-7
  1, 1, 0, 0, 0, 0, 0, 0,  // 8-9
  0, 1, 1, 1, 1, 1, 1, 1,  // A-G
  1, 1, 1, 1, 1, 1, 1, 1,  // H-O
  1, 1, 1, 1, 1, 1, 1, 1,  // P-W
  1, 1, 1, 0, 2, 0, 0, 1,  // X-Z, \ (special), _
  0, 1, 1, 1, 1, 1, 1, 1,  // a-g
  1, 1, 1, 1, 1, 1, 1, 1,  // h-o
  1, 1, 1, 1, 1, 1, 1, 1,  // p-w
  1, 1, 1, 0, 0, 0, 0, 0,  // x-z

// 128-255
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1,
};

static char lookup_op[256] = {
  _LOOKUP__EOF,  // 0, null
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__SPACE,  // 9, \t
  _LOOKUP__NEWLINE,  // 10, \n
  _LOOKUP__SPACE,
  _LOOKUP__SPACE,
  _LOOKUP__SPACE,  // 13, \r
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,  // 16
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,
  _LOOKUP__EOF,

  _LOOKUP__SPACE,  // 32, space
  _LOOKUP__OP_1,  // 33, !
  _LOOKUP__STRING,  // 34, "
  _LOOKUP__SYMBOL,  // 35, #
  _LOOKUP__SYMBOL,  // 36, $
  _LOOKUP__OP_1,  // 37, %
  _LOOKUP__OP_1,  // 38, &
  _LOOKUP__STRING,  // 39, '
  _LOOKUP__TOKEN | TOKEN_PAREN,  // 40, (
  _LOOKUP__TOKEN | TOKEN_CLOSE,  // 41, )
  _LOOKUP__OP_2,  // 42, *
  _LOOKUP__OP_1,  // 43, +
  _LOOKUP__COMMA,  // 44, ,
  _LOOKUP__OP_1,  // 45, -
  _LOOKUP__DOT,  // 46, .
  _LOOKUP__SLASH,  // 47, /

  _LOOKUP__NUMBER,  // 48, 0
  _LOOKUP__NUMBER,  // 49, 1
  _LOOKUP__NUMBER,  // 50, 2
  _LOOKUP__NUMBER,  // 51, 3
  _LOOKUP__NUMBER,  // 52, 4
  _LOOKUP__NUMBER,  // 53, 5
  _LOOKUP__NUMBER,  // 54, 6
  _LOOKUP__NUMBER,  // 55, 7
  _LOOKUP__NUMBER,  // 56, 8
  _LOOKUP__NUMBER,  // 57, 9

  _LOOKUP__TOKEN | TOKEN_COLON,  // 58, :
  _LOOKUP__TOKEN | TOKEN_SEMICOLON,  // 59, ;
  _LOOKUP__OP_2,  // 60, <
  _LOOKUP__OP_1,  // 61, =
  _LOOKUP__OP_3,  // 62, >
  _LOOKUP__Q,  // 63, ?
  _LOOKUP__EOF,  // 64, @

  _LOOKUP__SYMBOL, // 65, A
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,  // 90, Z

  _LOOKUP__TOKEN | TOKEN_ARRAY,  // 91, [
  _LOOKUP__EOF,  // 92, forward slash
  _LOOKUP__TOKEN | TOKEN_CLOSE,  // 93, ]
  _LOOKUP__OP_1,  // 94, ^
  _LOOKUP__SYMBOL,  // 95, _
  _LOOKUP__TEMPLATE,  // 96, '

  // nb. [h, j, k, m, q, x, z] don't start keywords
  _LOOKUP__LIT,  // 97, a
  _LOOKUP__LIT,  // 98, b
  _LOOKUP__LIT,  // 99, c
  _LOOKUP__LIT,  // 100, d
  _LOOKUP__LIT,  // 101, e
  _LOOKUP__LIT,  // 102, f
  _LOOKUP__LIT,  // 103, g
  _LOOKUP__SYMBOL,  // 104, h
  _LOOKUP__LIT,  // 105, i
  _LOOKUP__SYMBOL,  // 106, j
  _LOOKUP__SYMBOL,  // 107, k
  _LOOKUP__LIT,  // 108, l
  _LOOKUP__SYMBOL,  // 109, m
  _LOOKUP__LIT,  // 110, n
  _LOOKUP__LIT,  // 111, o
  _LOOKUP__LIT,  // 112, p
  _LOOKUP__SYMBOL,  // 113, q
  _LOOKUP__LIT,  // 114, r
  _LOOKUP__LIT,  // 115, s
  _LOOKUP__LIT,  // 116, t
  _LOOKUP__LIT,  // 117, u
  _LOOKUP__LIT,  // 118, v
  _LOOKUP__LIT,  // 119, w
  _LOOKUP__LIT,  // 120, y
  _LOOKUP__SYMBOL,  // 121, x
  _LOOKUP__SYMBOL,  // 122, z

  _LOOKUP__TOKEN | TOKEN_BRACE,  // 123, {
  _LOOKUP__OP_1,  // 124, |
  _LOOKUP__TOKEN | TOKEN_CLOSE,  // 125, }

  _LOOKUP__OP_1,  // 126, ~
  _LOOKUP__EOF,  // 127

// 128-255
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
  _LOOKUP__SYMBOL,
};

// global and shared with parser
#ifndef EMSCRIPTEN
tokendef _real_td;
#endif

// used by consume_template_string_inner to return to caller
static int litflag;

// expects pointer to be on start "/*"
static inline int internal_consume_multiline_comment(char *p, int *line_no) {
  if (p[0] != '/' || p[1] != '*') {
    return 0;
  }
  const char *start = p;
  p += 2;

  for (;;) {
    switch (p[0]) {
      case '\n':
        ++(*line_no);
        break;

      case '*':
        if (p[1] == '/') {
          return p - start + 2;
        }
        break;

      case '\0':
        return p - start;
    }
    ++p;
  }
}

static inline int consume_slash_op(char *p) {
  // can match "/" or "/="
  if (p[1] == '=') {
    return 2;
  }
  return 1;
}

static int consume_slash_regexp(char *p) {
  char *start = p;
  int is_charexpr = 0;

  for (;;) {
    ++p;

    switch (*p) {
      case '/':
        // nb. already known not to be a comment `//`
        if (is_charexpr) {
          break;
        }

        // eat trailing flags
        do {
          ++p;
        } while (isalnum(*p));

        // fall-through
      case '\0':
      case '\n':
        return (p - start);

      case '[':
        is_charexpr = 1;
        break;

      case ']':
        is_charexpr = 0;
        break;

      case '\\':
        ++p;  // ignore next char
    }
  }
}

static char *consume_space(char *p, int *line_no) {
  char c;
  for (;;) {
    c = *p;
    if (!isspace(c)) {
      return p;
    } else if (c == '\n') {
      ++(*line_no);
    }
    ++p;
  }
}

static int consume_basic_string(char *p, int *line_no) {
  int len = 0;

  for (;;) {
    char c = p[++len];
    switch (c) {
      case '\0':
        return len;

      case '\n':
        // nb. not valid here
        ++(*line_no);
        break;

      case '\\':
        c = p[++len];
        switch (c) {
          case '\0':
            return len;
          case '\n':
            ++(*line_no);
        }
        break;

      case '"':
      case '\'':
        if (c == p[0]) {
          return ++len;
        }
        break;
    }
  }
}

// p should point to first inner char, not `
static int consume_template_string_inner(char *p, int *line_no) {
  int len = 0;

  for (;;) {
    char c = p[len];

    switch (c) {
      case '\0':
        return len;

      case '\n':
        ++(*line_no);
        break;

      case '$':
        if (p[len+1] == '{') {
          litflag = 1;
          return len;
        }
        break;

      case '\\':
        c = p[++len];
        switch (c) {
          case '\0':
            return len;
          case '\n':
            ++(*line_no);
        }
        break;

      case '`':
        return ++len;
    }

    ++len;
  }

  return len;
}

// consumes number, assumes first char is valid (dot or digit)
static int consume_number(char *p) {
  int len = 1;
  char c = p[1];
  for (;;) {
    if (!(isalnum(c) || c == '.' || c == '_')) {  // letters, dots, etc- misuse is invalid, so eat anyway
      break;
    }
    c = p[++len];
  }
  return len;
}

static void eat_token(token *t, int *line_no) {
#define _ret(_len, _type) {t->type = _type; t->len = _len; return;};
#define _reth(_len, _type, _hash) {t->hash = _hash; t->type = _type; t->len = _len; return;};
  char *p = t->p;
  const char start = p[0];

  int len = 1;
  uint32_t hash = 0;
  int op = lookup_op[start];

  switch (op) {
    case _LOOKUP__SPACE:
    case _LOOKUP__NEWLINE:
      // TODO: for now, handled in consume_space
      _ret(0, ERROR__UNEXPECTED);

    case _LOOKUP__EOF:
      if (start != 0) {
        _ret(0, ERROR__UNEXPECTED);
      }
      _ret(0, TOKEN_EOF);

    case _LOOKUP__OP_1:
    case _LOOKUP__OP_2:
    case _LOOKUP__OP_3: {
      char c = p[len];
      while (len < op && c == start) {
        c = p[++len];
      }

      if (len == 1) {
        // simple cases that are hashed
        switch (start) {
          case '*':
            _reth(1, TOKEN_OP, MISC_STAR);
          case '~':
            _reth(1, TOKEN_OP, MISC_BITNOT);
          case '!':
            if (c != '=') {
              _reth(1, TOKEN_OP, MISC_NOT);
            }
            break;
        }

        // nb. these are all allowed=1, so len=1 even though we're consuming more
        if (start == '=' && c == '>') {
          _reth(2, TOKEN_OP, MISC_ARROW);  // arrow for arrow function
        } else if (c == start && (c == '+' || c == '-')) {
          // nb. we don't actually care which one this is
          _reth(2, TOKEN_OP, MISC_INCDEC);
        } else if (c == start && (c == '|' || c == '&')) {
          ++len;  // eat || or &&: but no more
        } else if (c == '=') {
          // consume a suffix '=' (or whole ===, !==)
          c = p[++len];
          if (c == '=' && (start == '=' || start == '!')) {
            ++len;
          }
        } else if (start == '=') {
          // match equals specially
          _reth(1, TOKEN_OP, MISC_EQUALS);
        }
      }

      _ret(len, TOKEN_OP);
    }

    case _LOOKUP__DOT:
      if (isdigit(p[1])) {
        _ret(consume_number(p), TOKEN_NUMBER);
      } else if (p[1] == '.' && p[2] == '.') {
        _reth(3, TOKEN_OP, MISC_SPREAD);  // '...' operator
      }
      _reth(1, TOKEN_OP, MISC_DOT);

    case _LOOKUP__SLASH:
      switch (p[1]) {
        case '/':
          _ret(strline(p), TOKEN_COMMENT);
        case '*':
          _ret(internal_consume_multiline_comment(p, line_no), TOKEN_COMMENT);
      }
      _ret(1, TOKEN_SLASH);  // ambigious

    case _LOOKUP__Q:
      switch (p[1]) {
        case '.':
          _reth(2, TOKEN_OP, MISC_CHAIN);
        case '?':
          _ret(2, TOKEN_OP);
      }
      _ret(1, TOKEN_TERNARY);

    case _LOOKUP__COMMA:
      _reth(1, TOKEN_OP, MISC_COMMA);

    case _LOOKUP__STRING:
      litflag = 0;
      _ret(consume_basic_string(p, line_no), TOKEN_STRING);

    case _LOOKUP__TEMPLATE: {
      litflag = 0;
      _ret(consume_template_string_inner(p + 1, line_no) + 1, TOKEN_STRING);
    }

    case _LOOKUP__LIT:
      len = consume_known_lit(p, &hash);
      // fall-through

    case _LOOKUP__SYMBOL: {
      char c = p[len];
      if (!lookup_symbol[c]) {
        // this checks the symbol after lit (above), or p[1]
        _reth(len, TOKEN_LIT, hash);
      }

      for (;;) {
        if (c == '\\') {
          char escape = p[++len];
          switch (escape) {
            case '\0':
              _ret(len, TOKEN_LIT);  // don't escape null

            case 'u':
              if (p[len + 1] != '{') {
                break;
              }
              // TODO: match \u{1234}
              _ret(0, ERROR__INTERNAL);
          }
        }
        c = p[++len];

        // check if we can continue
        if (!lookup_symbol[c]) {
          _ret(len, TOKEN_LIT);
        }
      }
    }

    case _LOOKUP__NUMBER:
      _ret(consume_number(p), TOKEN_NUMBER);
  }

  _ret(1, op & ~(_LOOKUP__TOKEN));
#undef _ret
#undef _reth
}

int prsr_next() {
  switch (td->flag) {
    case FLAG__PENDING_T_BRACE:
      td->cursor.p += td->cursor.len;  // move past string
      td->cursor.type = TOKEN_T_BRACE;
      td->cursor.len = 2;
      td->cursor.hash = 0;
      td->flag = 0;

      if (td->depth == __STACK_SIZE - 1) {
        return ERROR__STACK;
      }
      td->stack[td->depth++] = TOKEN_T_BRACE;

      return TOKEN_T_BRACE;
    case FLAG__RESUME_LIT: {
      litflag = 0;
      ++td->cursor.p;  // move past '}' (i.e., previous TOKEN_CLOSE)
      td->cursor.type = TOKEN_STRING;
      td->cursor.len = consume_template_string_inner(td->cursor.p, &(td->line_no));
      td->cursor.hash = 0;
      td->flag = litflag ? FLAG__PENDING_T_BRACE : 0;
      return TOKEN_STRING;
    }
  }

  if (td->cursor.len == 0 && td->cursor.type != TOKEN_UNKNOWN) {
    // TODO: this could also be "TOKEN_OP" check
    return ERROR__INTERNAL;
  }

  token cursor;
  cursor.hash = 0;

  // consume space chars after previous, until next token
  char *p = consume_space(td->cursor.p + td->cursor.len, &td->line_no);
  cursor.p = p;
  cursor.line_no = td->line_no;

  eat_token(&cursor, &(td->line_no));
  int ret = cursor.type;

  switch (cursor.type) {
    case TOKEN_STRING:
      if (litflag) {
        td->flag = FLAG__PENDING_T_BRACE;
      }
      break;

    case TOKEN_COLON:
      // inside ternary stack, close it
      if (td->depth && td->stack[td->depth - 1] == TOKEN_TERNARY) {
        cursor.type = TOKEN_CLOSE;
        --td->depth;
      }
      break;

    case TOKEN_TERNARY:
    case TOKEN_PAREN:
    case TOKEN_ARRAY:
    case TOKEN_BRACE:
      if (td->depth == __STACK_SIZE - 1) {
        ret = ERROR__STACK;
        break;
      }
      td->stack[td->depth++] = cursor.type;
      break;

    case TOKEN_CLOSE:
      if (!td->depth) {
        ret = ERROR__STACK;
        break;
      }
      uint8_t type = td->stack[--td->depth];
      if (type == TOKEN_T_BRACE) {
        td->flag |= FLAG__RESUME_LIT;
      }
      break;
  }

  // this doesn't clear peek, so it's still valid (should be ~= this token)
  td->cursor = cursor;
  return ret;
}

void prsr_init_token(char *p) {
  bzero(td, sizeof(tokendef));
  td->line_no = 1;

  td->cursor.type = TOKEN_UNKNOWN;
  td->cursor.p = p;
  td->peek.type = TOKEN_UNKNOWN;
  td->peek.p = p;
}

int prsr_update(int type) {
  switch (td->cursor.type) {
    case TOKEN_SLASH:
      switch (type) {
        case TOKEN_OP:
          td->cursor.len = consume_slash_op(td->cursor.p);
          break;
        case TOKEN_REGEXP:
          td->cursor.len = consume_slash_regexp(td->cursor.p);
          break;
        default:
          return ERROR__INTERNAL;
      }
      td->cursor.type = type;
      return 0;

    case TOKEN_LIT:
      if (!(type == TOKEN_SYMBOL || type == TOKEN_KEYWORD || type == TOKEN_LABEL || type == TOKEN_OP)) {
        return ERROR__INTERNAL;
      }
      td->cursor.type = type;
      return 0;

    default:
      return ERROR__INTERNAL;
  }
}

int prsr_peek() {
  if (td->peek.p > td->cursor.p) {
    return td->peek.type;
  }

  td->peek.p = td->cursor.p + td->cursor.len;
  td->peek.hash = 0;

  // nb. we never care about template strings in peek
  // cursor can be zero at EOF
  if (td->flag || td->cursor.len == 0) {
    td->peek.type = TOKEN_UNKNOWN;
    return TOKEN_UNKNOWN;
  }

  static int line_no = 0;  // never used, just needed as valid ptr
  for (;;) {
    td->peek.p = consume_space(td->peek.p, &(line_no));
    eat_token(&(td->peek), &(line_no));

    int type = td->peek.type;
    if (type != TOKEN_COMMENT) {
      return type;
    }

    td->peek.p += td->peek.len;
  }
}
