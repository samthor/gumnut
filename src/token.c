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

// global
tokendef td;

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

static int consume_string(char *p, int *line_no, int *litflag) {
  int len;
  char start;
  if (*litflag) {
    len = -1;
    start = '`';
    *litflag = 0;
  } else {
    len = 0;
    start = p[0];
  }

  for (;;) {
    char c = p[++len];
    if (c == start) {
      ++len;
      return len;
    }

    switch (c) {
      case '\0':
        return len;

      case '$':
        if (start == '`' && p[len+1] == '{') {
          *litflag = 1;
          return len;
        }
        break;

      case '\\':
        c = p[++len];
        if (c == '\n') {
          ++(*line_no);  // record if newline (this is valid in all string types)
        }
        break;

      case '\n':
        // invalid in non-` but not much else we can do
        ++(*line_no);
        break;
    }
  }

  return len;
}

static void eat_token(token *t, int *line_no) {
#define _ret(_len, _type) {t->type = _type; t->len = _len; return;};
#define _reth(_len, _type, _hash) {t->type = _type; t->len = _len; t->hash = _hash; return;};
  char *p = t->p;
  const char start = p[0];

  // simple cases
  switch (start) {
    case '\0':
      _ret(0, TOKEN_EOF);

    case '/':
      switch (p[1]) {
        case '/':
          _ret(strline(p), TOKEN_COMMENT);
        case '*':
          _ret(internal_consume_multiline_comment(p, line_no), TOKEN_COMMENT);
      }
      _ret(1, TOKEN_SLASH);  // ambigious

    case ';':
      _ret(1, TOKEN_SEMICOLON);

    case '?':
      switch (p[1]) {
        case '.':
          _reth(2, TOKEN_OP, MISC_CHAIN);
        case '?':
          _ret(2, TOKEN_OP);
      }
      _ret(1, TOKEN_TERNARY);

    case ':':
      _reth(1, TOKEN_COLON, MISC_COLON);  // nb. might change to TOKEN_CLOSE in parent

    case ',':
      _reth(1, TOKEN_OP, MISC_COMMA);

    case '{':
      _ret(1, TOKEN_BRACE);

    case '(':
      _ret(1, TOKEN_PAREN);

    case '[':
      _ret(1, TOKEN_ARRAY);

    case ']':
      _reth(1, TOKEN_CLOSE, MISC_RARRAY);

    case ')':
    case '}':
      _ret(1, TOKEN_CLOSE);

    case '\'':
    case '"':
    case '`': {
      int litflag = 0;  // TODO: not used
      int len = consume_string(p, line_no, &litflag);
      _ret(len, TOKEN_STRING);
    }
  }

  // ops: i.e., anything made up of =<& etc (except '/' and ',', handled above)
  // note: 'in' and 'instanceof' are ops in most cases, but here they are lit
  do {
    char c = start;
    int len = 0;
    int allowed;  // how many ops of the same type we can safely consume

    if (memchr("=&|^~!%+-", start, 9)) {
      allowed = 1;
    } else if (start == '*' || start == '<') {
      allowed = 2;  // exponention operator **, or shift
    } else if (start == '>') {
      allowed = 3;  // right shift, or zero-fill right shift
    } else {
      break;
    }

    while (len < allowed) {
      c = p[++len];
      if (c != start) {
        break;
      }
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
        // nb. we don't actaully care which one this is?
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
  } while (0);

  // number: "0", ".01", "0x100"
  const char next = p[1];
  if (isdigit(start) || (start == '.' && isdigit(next))) {
    int len = 1;
    char c = next;
    for (;;) {
      if (!(isalnum(c) || c == '.')) {  // letters, dots, etc- misuse is invalid, so eat anyway
        break;
      }
      c = p[++len];
    }
    _ret(len, TOKEN_NUMBER);
  }

  // dot notation (not a number)
  if (start == '.') {
    if (next == '.' && p[2] == '.') {
      _reth(3, TOKEN_OP, MISC_SPREAD);  // '...' operator
    }
    _reth(1, TOKEN_OP, MISC_DOT);  // it's valid to say e.g., "foo . bar", so separate token
  }

  // literals
  uint32_t hash = 0;
  int len;
  if (start == '#') {
    len = 1;  // allow # at start of literal, for private vars
  } else {
    len = consume_known_lit(p, &hash);
  }
  char c = p[len];
  do {
    // FIXME: escapes aren't valid in literals, but check whether this matches UTF-8
    if (c == '\\') {
      hash = 0;
      ++len;  // don't care, eat whatever aferwards
      c = p[++len];
      if (c != '{') {
        continue;
      }
      while (c && c != '}') {
        c = p[++len];
      }
      ++len;
      continue;
    }

    // nb. `c < 0` == `((unsigned char) c) > 127`
    int valid = (len ? isalnum(c) : isalpha(c)) || c == '$' || c == '_' || c < 0;
    if (!valid) {
      break;
    }
    hash = 0;
    c = p[++len];
  } while (c);

  if (!len) {
    // found nothing :(
    _ret(0, -1);
  }

  _reth(len, TOKEN_LIT, hash);
#undef _ret
#undef _reth
}

int prsr_next() {
  switch (td.flag) {
    case FLAG__PENDING_T_BRACE:
      td.cursor.p += td.cursor.len;  // move past string
      td.cursor.type = TOKEN_T_BRACE;
      td.cursor.len = 2;
      td.cursor.hash = 0;
      td.flag = 0;

      if (td.depth == __STACK_SIZE - 1) {
        return ERROR__STACK;
      }
      td.stack[td.depth++] = TOKEN_T_BRACE;

      return TOKEN_T_BRACE;
    case FLAG__RESUME_LIT: {
      int litflag = 1;
      ++td.cursor.p;  // move past '}' (i.e., previous TOKEN_CLOSE)
      td.cursor.type = TOKEN_STRING;
      td.cursor.len = consume_string(td.cursor.p, &td.line_no, &litflag);
      td.cursor.hash = 0;
      td.flag = litflag ? FLAG__PENDING_T_BRACE : 0;
      return TOKEN_STRING;
    }
  }

  if (td.cursor.len == 0 && td.cursor.type != TOKEN_UNKNOWN) {
    // TODO: this could also be "TOKEN_OP" check
    return ERROR__INTERNAL;
  }

  token cursor;
  cursor.hash = 0;

  // consume space chars after previous, until next token
  char *p = consume_space(td.cursor.p + td.cursor.len, &td.line_no);
  cursor.p = p;
  cursor.line_no = td.line_no;

  eat_token(&cursor, &(td.line_no));
  int ret = cursor.type;

  switch (cursor.type) {
    case TOKEN_STRING:
      if (p[0] == '`' && (cursor.len == 1 || p[cursor.len - 1] != '`')) {
        td.flag = FLAG__PENDING_T_BRACE;
      }
      break;

    case TOKEN_COLON:
      // inside ternary stack, close it
      if (td.depth && td.stack[td.depth - 1] == TOKEN_TERNARY) {
        cursor.type = TOKEN_CLOSE;
        --td.depth;
      }
      break;

    case TOKEN_TERNARY:
    case TOKEN_PAREN:
    case TOKEN_ARRAY:
    case TOKEN_BRACE:
      if (td.depth == __STACK_SIZE - 1) {
        ret = ERROR__STACK;
        break;
      }
      td.stack[td.depth++] = cursor.type;
      break;

    case TOKEN_CLOSE:
      if (!td.depth) {
        ret = ERROR__STACK;
        break;
      }
      uint8_t type = td.stack[--td.depth];
      if (type == TOKEN_T_BRACE) {
        td.flag |= FLAG__RESUME_LIT;
      }
      break;
  }

  // this doesn't clear peek, so it's still valid (should be ~= this token)
  td.cursor = cursor;
  return ret;
}

void prsr_init_token(char *p) {
  bzero(&td, sizeof(td));
  td.line_no = 1;

  td.cursor.type = TOKEN_UNKNOWN;
  td.cursor.p = p;
  td.peek.type = TOKEN_UNKNOWN;
  td.peek.p = p;
}

int prsr_update(int type) {
  switch (td.cursor.type) {
    case TOKEN_SLASH:
      switch (type) {
        case TOKEN_OP:
          td.cursor.len = consume_slash_op(td.cursor.p);
          break;
        case TOKEN_REGEXP:
          td.cursor.len = consume_slash_regexp(td.cursor.p);
          break;
        default:
          return ERROR__INTERNAL;
      }
      td.cursor.type = type;
      return 0;

    case TOKEN_LIT:
      if (!(type == TOKEN_SYMBOL || type == TOKEN_KEYWORD || type == TOKEN_LABEL || type == TOKEN_OP)) {
        return ERROR__INTERNAL;
      }
      td.cursor.type = type;
      return 0;

    default:
      return ERROR__INTERNAL;
  }
}

int prsr_peek() {
  if (td.peek.p > td.cursor.p) {
    return td.peek.type;
  }

  td.peek.p = td.cursor.p + td.cursor.len;
  td.peek.hash = 0;

  // nb. we never care about template strings in peek
  // cursor can be zero at EOF
  if (td.flag || td.cursor.len == 0) {
    td.peek.type = TOKEN_UNKNOWN;
    return TOKEN_UNKNOWN;
  }

  static int line_no = 0;  // never used, just needed as valid ptr
  for (;;) {
    td.peek.p = consume_space(td.peek.p, &(line_no));
    eat_token(&(td.peek), &(line_no));

    int type = td.peek.type;
    if (type != TOKEN_COMMENT) {
      return type;
    }

    td.peek.p += td.peek.len;
  }
}
