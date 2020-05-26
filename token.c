
#include <ctype.h>
#include <string.h>
#include "token.h"
#include "tokens/lit.h"
#include "tokens/helper.h"

#define FLAG__PENDING_T_BRACE 1
#define FLAG__RESUME_LIT      2

// expects pointer to be on first "*"
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

    case '/': {
      char *end;

      switch (p[1]) {
        case '/':
          end = strchr(p, '\n');
          int len;
          if (end) {
            len = end - p;
          } else {
            len = strlen(p);
          }
          _ret(len, TOKEN_COMMENT);
        case '*':
          end = internal_consume_multiline_comment(p + 1, line_no);
          _ret(end - p, TOKEN_COMMENT);
      }
      _ret(1, TOKEN_SLASH);  // ambigious
    }

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
        _reth(2, TOKEN_ARROW, MISC_ARROW);  // arrow for arrow function
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

int prsr_next(tokendef *d) {
  switch (d->flag) {
    case FLAG__PENDING_T_BRACE:
      d->cursor.p += d->cursor.len;  // move past string
      d->cursor.type = TOKEN_T_BRACE;
      d->cursor.len = 2;
      d->cursor.hash = 0;
      d->flag = 0;

      if (d->depth == __STACK_SIZE - 1) {
        return ERROR__STACK;
      }
      d->stack[d->depth++] = TOKEN_T_BRACE;

      return TOKEN_T_BRACE;
    case FLAG__RESUME_LIT: {
      int litflag = 1;
      ++d->cursor.p;  // move past '}'
      d->cursor.type = TOKEN_STRING;
      d->cursor.len = consume_string(d->cursor.p, &d->line_no, &litflag);
      d->cursor.hash = 0;
      d->flag = litflag ? FLAG__PENDING_T_BRACE : 0;
      return TOKEN_STRING;
    }
  }

  if (d->cursor.len == 0 && d->cursor.type != TOKEN_UNKNOWN) {
    // TODO: this could also be "TOKEN_OP" check
    return ERROR__VALUE;
  }

  token cursor;
  cursor.hash = 0;

  // consume space chars after previous, until next token
  char *p = consume_space(d->cursor.p + d->cursor.len, &d->line_no);
  cursor.p = p;
  cursor.line_no = d->line_no;

  eat_token(&cursor, &(d->line_no));
  int ret = cursor.type;

  switch (cursor.type) {
    case TOKEN_STRING:
      if (p[0] == '`' && (cursor.len == 1 || p[cursor.len - 1] != '`')) {
        d->flag = FLAG__PENDING_T_BRACE;
      }
      break;

    case TOKEN_COLON:
      // inside ternary stack, close it
      if (d->depth && d->stack[d->depth - 1] == TOKEN_TERNARY) {
        cursor.type = TOKEN_CLOSE;
        --d->depth;
      }
      break;

    case TOKEN_TERNARY:
    case TOKEN_PAREN:
    case TOKEN_ARRAY:
    case TOKEN_BRACE:
      if (d->depth == __STACK_SIZE - 1) {
        ret = ERROR__STACK;
        break;
      }
      d->stack[d->depth++] = cursor.type;
      break;

    case TOKEN_CLOSE:
      if (!d->depth) {
        ret = ERROR__STACK;
        break;
      }
      uint8_t type = d->stack[--d->depth];
      if (type == TOKEN_T_BRACE) {
        d->flag |= FLAG__RESUME_LIT;
      }
      break;
  }

  d->cursor = cursor;
  d->peek.type = TOKEN_UNKNOWN;
  return ret;
}

tokendef prsr_init_token(char *p) {
  tokendef d;
  bzero(&d, sizeof(d));
  d.buf = p;
  d.line_no = 1;

  d.cursor.type = TOKEN_UNKNOWN;
  d.cursor.p = p;
  d.peek.type = TOKEN_UNKNOWN;
  d.peek.p = p;

  return d;
}

int prsr_update(tokendef *d, int type) {
  switch (d->cursor.type) {
    case TOKEN_SLASH:
      switch (type) {
        case TOKEN_OP:
          d->cursor.len = consume_slash_op(d->cursor.p);
          break;
        case TOKEN_REGEXP:
          d->cursor.len = consume_slash_regexp(d->cursor.p);
          break;
        default:
          return ERROR__INTERNAL;
      }
      d->cursor.type = type;
      return 0;

    case TOKEN_LIT:
      if (!(type == TOKEN_SYMBOL || type == TOKEN_KEYWORD || type == TOKEN_LABEL || type == TOKEN_OP)) {
        return ERROR__INTERNAL;
      }
      d->cursor.type = type;
      return 0;

    default:
      return ERROR__INTERNAL;
  }
}

int prsr_peek(tokendef *d) {
  if (d->peek.type != TOKEN_UNKNOWN) {
    return d->peek.type;
  } else if (d->cursor.len == 0) {
    return TOKEN_UNKNOWN;
  }

  char *p = d->cursor.p + d->cursor.len;
  d->peek.line_no = d->line_no;
  d->peek.hash = 0;

  switch (d->flag) {
    case FLAG__PENDING_T_BRACE:
      d->peek.p = p;
      d->peek.len = 2;
      d->peek.type = TOKEN_T_BRACE;
      return TOKEN_T_BRACE;

    case FLAG__RESUME_LIT:
      d->peek.p = p;
      d->peek.len = 0;  // TODO: we don't bother in peek
      d->peek.type = TOKEN_STRING;
      return TOKEN_STRING;
  }

  for (;;) {
    d->peek.p = consume_space(p, &(d->peek.line_no));
    eat_token(&(d->peek), &(d->peek.line_no));

    int type = d->peek.type;
    if (type != TOKEN_COMMENT) {
      return type;
    }
  }
}
