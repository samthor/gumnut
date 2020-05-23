/*
 * Copyright 2019 Sam Thorogood. All rights reserved.
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
#include "tokens/lit.h"
#include "tokens/helper.h"
#include "token.h"

#define FLAG__PENDING_T_BRACE 1
#define FLAG__RESUME_LIT      2

typedef struct {
  int len;
  int type;
  uint32_t hash;
} eat_out;

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
      case 0:
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
      case 0:
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
        if (start != '`') {
          return len;  // invalid, but we consumed partial string until newline
        }
        ++(*line_no);
        break;
    }
  }

  return len;
}

static eat_out eat_token(char *p, token *prev) {
#define _ret(_len, _type) ((eat_out) {_len, _type, 0});
#define _reth(_len, _type, _hash) ((eat_out) {_len, _type, _hash});
  const char start = p[0];

  // simple cases
  switch (start) {
    case 0:
      return _ret(0, TOKEN_EOF);

    case '/':
      if (prev->hash == MISC_RARRAY) {
        // end of array [], always op
        return _ret(consume_slash_op(p), TOKEN_OP);
      }

      switch (prev->type) {
        case TOKEN_OP:
          if (prev->hash == MISC_INCDEC) {
            break;  // weird attachment rules
          }
          // fall-through

        case TOKEN_EOF:
        case TOKEN_EXEC:     // not generated
        case TOKEN_SEMICOLON:
        case TOKEN_ARROW:
        case TOKEN_COLON:    // label or ternary, both imply regexp
        case TOKEN_DICT:     // not generated (and invalid)
        case TOKEN_ARRAY:
        case TOKEN_PAREN:
        case TOKEN_T_BRACE:
        case TOKEN_TERNARY:
        case TOKEN_KEYWORD:  // not generated
          return _ret(consume_slash_regexp(p), TOKEN_REGEXP);

        case TOKEN_LIT:
          if (prev->hash) {
            if (prev->hash & (_MASK_REL_OP | _MASK_UNARY_OP | _MASK_KEYWORD)) {
              // "in", "delete" etc always take arg on right
              // always keyword is invalid, but treat as regexp
              return _ret(consume_slash_regexp(p), TOKEN_REGEXP);
            }
            break;  // who knows
          }
          // fall-through

        case TOKEN_REGEXP:
        case TOKEN_NUMBER:
        case TOKEN_STRING:
        case TOKEN_SYMBOL:   // not generated
          return _ret(consume_slash_op(p), TOKEN_OP);

#ifdef DEBUG
        case TOKEN_CLOSE:
        case TOKEN_LABEL:    // not generated (and invalid)
          break;  // ambiguous
#endif
      }

      // unkown, return ambiguous
      return _ret(1, TOKEN_SLASH);  // return ambig, handled elsewhere

    case ';':
      return _ret(1, TOKEN_SEMICOLON);

    case '?':
      switch (p[1]) {
        case '.':
          return _reth(2, TOKEN_OP, MISC_CHAIN);
        case '?':
          return _ret(2, TOKEN_OP);
      }
      return _ret(1, TOKEN_TERNARY);

    case ':':
      return _reth(1, TOKEN_COLON, MISC_COLON);  // nb. might change to TOKEN_CLOSE in parent

    case ',':
      return _reth(1, TOKEN_OP, MISC_COMMA);

    case '{':
      return _ret(1, TOKEN_BRACE);

    case '(':
      return _ret(1, TOKEN_PAREN);

    case '[':
      return _ret(1, TOKEN_ARRAY);

    case ']':
      return _reth(1, TOKEN_CLOSE, MISC_RARRAY);

    case ')':
    case '}':
      return _ret(1, TOKEN_CLOSE);

    case '\'':
    case '"':
    case '`':
      return _ret(0, TOKEN_STRING);  // consumed by parent

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
          return _reth(1, TOKEN_OP, MISC_STAR);
        case '~':
          return _reth(1, TOKEN_OP, MISC_BITNOT);
        case '!':
          if (c != '=') {
            return _reth(1, TOKEN_OP, MISC_NOT);
          }
          break;
      }

      // nb. these are all allowed=1, so len=1 even though we're consuming more
      if (start == '=' && c == '>') {
        return _reth(2, TOKEN_ARROW, MISC_ARROW);  // arrow for arrow function
      } else if (c == start && (c == '+' || c == '-')) {
        // nb. we don't actaully care which one this is?
        return _reth(2, TOKEN_OP, MISC_INCDEC);
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
        return _reth(1, TOKEN_OP, MISC_EQUALS);
      }
    }

    return _ret(len, TOKEN_OP);
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
    return _ret(len, TOKEN_NUMBER);
  }

  // dot notation (not a number)
  if (start == '.') {
    if (next == '.' && p[2] == '.') {
      return _reth(3, TOKEN_OP, MISC_SPREAD);  // '...' operator
    }
    return _reth(1, TOKEN_OP, MISC_DOT);  // it's valid to say e.g., "foo . bar", so separate token
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
    return _ret(0, -1);
  }

  int type = TOKEN_LIT;
  if (prev->hash == MISC_DOT || prev->hash == MISC_CHAIN) {
    // in foo.bar, bar is always a symbol (even if it's a reserved word)
    type = TOKEN_SYMBOL;
  }
  return _reth(len, type, hash);
#undef _ret
#undef _reth
}

static inline char *internal_consume_multiline_comment(char *p, int *line_no) {
  for (;;) {
    char c = *(++p);
    switch (c) {
      case '\n':
        ++(*line_no);
        break;

      case '*':
        if (p[1] == '/') {
          return p + 2;
        }
        break;

      case 0:
        return p;
    }
  }
}

static int consume_comment(char *p, int *line_no, int start) {
  char *from = p;

  switch (*p) {
    case '/': {
      char next = *(++p);
      if (next == '*') {
        return internal_consume_multiline_comment(p, line_no) - from;
      } else if (next != '/') {
        return 0;
      }
      break;
    }

    case '#':
      // consume #! at top of file
      if (!(start && *(++p) == '!')) {
        return 0;
      }
      break;

    default:
      return 0;
  }

  // match single-line comment
  for (;;) {
    char c = *p;
    if (c == '\n' || !c) {
      break;
    }
    ++p;
  }
  return p - from;
}

static char *consume_space(char *p, int *line_no) {
  char c;
#define _check() \
    c = *p; \
    if (!isspace(c)) { \
      return p; \
    } else if (c == '\n') { \
      ++(*line_no); \
    } \
    ++p;

#if 1
  for (;;) {
    _check();
  }
#else
// This could potentially be faster but gives no performance gain in native mode (about the same or
// potentially marginally slower). It's actually worse in 32-bit.
  int off = ((int) p & 3);
  switch (off) {
    case 1: _check();
    case 2: _check();
    case 3: _check();
    case 4: _check();
    case 5: _check();
    case 6: _check();
    case 7: _check();
  }
  uint64_t *words = (uint64_t *) p;

  for (;;) {
    if (*words == 0x2020202020202020) {
      p += 8;
    } else {
      #pragma unroll
      for (int i = 0; i < 8; ++i) {
        _check();
      }
    }
    ++words;
  }
#endif
#undef _check
}

static void eat_next(tokendef *d) {
  // consume from next, repeat(space, comment [first into pending]) and next token
  char *from = d->next.p + d->next.len;

  // short-circuit for token state machine
  if (d->flag) {
    if (d->flag == FLAG__PENDING_T_BRACE) {
      d->next.type = TOKEN_T_BRACE;
      d->next.len = 2;  // "${"
      d->flag = 0;
    } else if (d->flag == FLAG__RESUME_LIT) {
      int litflag = 1;
      d->next.type = TOKEN_STRING;
      d->next.len = consume_string(from, &d->line_no, &litflag);
      d->flag = litflag ? FLAG__PENDING_T_BRACE : 0;
    }
    d->next.p = from;
    return;
  }

  // always consume space chars
  char *p = consume_space(from, &d->line_no);
  d->pending.p = p;
  d->pending.line_no = d->line_no;

  // match comments (C99 and long), record first in pending
  int len = consume_comment(p, &d->line_no, p == d->buf);
  d->pending.len = len;
  d->line_after_pending = d->line_no;
  while (len) {
    p += len;
    p = consume_space(p, &d->line_no);
    len = consume_comment(p, &d->line_no, 0);
  }

  // match real token
  eat_out eat = eat_token(p, &(d->next));
  d->next.type = eat.type;
  d->next.hash = eat.hash;
  d->next.line_no = d->line_no;
  d->next.p = p;
  d->next.len = eat.len;

  // special-case token
  switch (eat.type) {
    case TOKEN_EOF:
      d->next.line_no = 0;  // always change line_no for EOF
      break;

    case TOKEN_STRING: {
      // consume string
      int litflag = 0;
      d->next.len = consume_string(p, &d->line_no, &litflag);
      if (litflag) {
        d->flag = FLAG__PENDING_T_BRACE;
      }
      break;
    }

    case TOKEN_COLON:
      // inside ternary stack, close it
      if (d->depth && d->stack[d->depth - 1] == TOKEN_TERNARY) {
        d->next.type = TOKEN_CLOSE;
      }
      break;
  }
}

int prsr_next_token(tokendef *d, token *out, int has_value) {
  if (d->pending.len) {
    // copy pending comment out, try to yield more
    memcpy(out, &d->pending, sizeof(token));

    char *p = consume_space(d->pending.p + d->pending.len, &d->line_after_pending);
    if (p == d->next.p) {
      d->pending.len = 0;
      return 0;  // nothing to do, reached real token
    }

    // queue up upcoming comment
    d->pending.p = p;
    d->pending.line_no = d->line_after_pending;
    d->pending.len = consume_comment(p, &d->line_after_pending, 0);

    if (!d->pending.len) {
      return ERROR__INTERNAL;
    }
    return 0;
  }

  memcpy(out, &d->next, sizeof(token));

  // actually enact token
  switch (out->type) {
    case TOKEN_SLASH:
      // consume this token as lookup can't know what it was
      if (out->p[0] != '/' || has_value < 0) {
        return ERROR__VALUE;
      } else if (has_value) {
        out->type = TOKEN_OP;
        out->len = consume_slash_op(out->p);
      } else {
        out->type = TOKEN_REGEXP;
        out->len = consume_slash_regexp(out->p);
      }
      d->next.len = out->len;
      break;

    case TOKEN_TERNARY:
    case TOKEN_PAREN:
    case TOKEN_ARRAY:
    case TOKEN_BRACE:
    case TOKEN_T_BRACE:
      if (d->depth == __STACK_SIZE - 1) {
        eat_next(d);  // consume invalid open but return error
        return ERROR__STACK;
      }
      d->stack[d->depth++] = out->type;
      break;

    case TOKEN_CLOSE:
      if (!d->depth) {
        eat_next(d);  // consume invalid close but return error
        return ERROR__STACK;
      }
      uint8_t type = d->stack[--d->depth];
      if (type == TOKEN_T_BRACE) {
        d->flag |= FLAG__RESUME_LIT;
      }
      break;
  }

  eat_next(d);
  return 0;
}

void prsr_close_op_next(tokendef *d) {
  if (d->next.type == TOKEN_OP && d->next.p[0] == '/') {
    // change to TOKEN_REGEXP
    char *p = d->next.p - d->next.len;
    d->next.len = consume_slash_regexp(p);
    d->next.type = TOKEN_REGEXP;
  }
}

tokendef prsr_init_token(char *p) {
  tokendef d;
  bzero(&d, sizeof(d));
  d.buf = p;
  d.line_no = 1;

  d.pending.type = TOKEN_COMMENT;
  d.next.p = p;  // place next cursor

  eat_next(&d);
  return d;
}
