#include <ctype.h>
#include <string.h>
#include "token.h"
#include "utils.h"

#define FLAG__PENDING_T_BRACE 1
#define FLAG__RESUME_LIT      2

typedef struct {
  int len;
  int type;
} eat_out;

static void stack_inc(tokendef *d, int t_brace) {
  uint32_t *p = d->stack + (d->depth >> 5);
  uint32_t set = (uint32_t) 1 << (d->depth & 31);
  if (t_brace) {
    *p |= set;  // set bit
  } else {
    *p &= ~set; // clear bit
  }
  ++d->depth;
}

static int stack_dec(tokendef *d) {
  --d->depth;

  uint32_t *p = d->stack + (d->depth >> 5);
  uint32_t check = (uint32_t) 1 << (d->depth & 31);

  return *p & check;
}

static int consume_slash_op(char *p) {
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
      break;
    } else if (start == '`' && c == '$' && p[len+1] == '{') {
      *litflag = 1;
      break;
    } else if (c == '\n') {
      // FIXME: only allowed in template literals
      ++(*line_no);
    }
  }

  return len;
}

static eat_out eat_token(char *p) {
#define _ret(_len, _type) ((eat_out) {_len, _type});

  // look for EOF
  const char start = p[0];
  if (!start) {
    return _ret(0, TOKEN_EOF);
  }

  // simple ascii characters
  switch (start) {
    case '/':
      return _ret(1, TOKEN_SLASH);  // return ambig, handled elsewhere

    case ';':
      return _ret(1, TOKEN_SEMICOLON);

    case '?':
      return _ret(1, TOKEN_TERNARY);

    case ':':
      return _ret(1, TOKEN_COLON);

    case ',':
      return _ret(1, TOKEN_COMMA);

    case '(':
      return _ret(1, TOKEN_PAREN);

    case '[':
      return _ret(1, TOKEN_ARRAY);

    case '{':
      return _ret(1, TOKEN_BRACE);

    case ')':
    case ']':
    case '}':
      return _ret(1, TOKEN_CLOSE);
  }

  // ops: i.e., anything made up of =<& etc (except '/', handled above)
  // note: 'in' and 'instanceof' are ops in most cases, but here they are lit
  do {
    char c = start;
    int len = 0;
    int allowed;  // how many ops of the same type we can safely consume

    if (strchr("=&|^~!%+-", start)) {
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

    if (start == '=' && c == '>') {
      return _ret(2, TOKEN_ARROW);  // arrow for arrow function
    } else if (c == start && strchr("+-|&", start)) {
      ++len;  // eat --, ++, || or &&: but no more
    } else if (c == '=') {
      // consume a suffix '=' (or whole ===, !==)
      c = p[++len];
      if (c == '=' && (start == '=' || start == '!')) {
        ++len;
      }
    }

    return _ret(len, TOKEN_OP);
  } while (0);

  // strings (handled by parent)
  if (start == '\'' || start == '"' || start == '`') {
    return _ret(0, TOKEN_STRING);
  }

  // number: "0", ".01", "0x100"
  const char next = p[1];
  if (isnum(start) || (start == '.' && isnum(next))) {
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

  // dot notation
  if (start == '.') {
    if (next == '.' && p[2] == '.') {
      return _ret(3, TOKEN_SPREAD);  // '...' operator
    }
    return _ret(1, TOKEN_DOT);  // it's valid to say e.g., "foo . bar", so separate token
  }

  // literals
  int len = 0;
  char c = start;
  do {
    // FIXME: escapes aren't valid in literals, but check whether this matches UTF-8
    if (c == '\\') {
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
    c = p[++len];
  } while (c);

  if (len) {
    // we don't care what this is, give to caller
    return _ret(len, TOKEN_LIT);
  }

  // found nothing :(
  return _ret(0, -1);
#undef _ret
}

static int consume_comment(char *p, int *line_no) {
  if (*p != '/') {
    return 0;
  }

  char *find;
  switch (p[1]) {
    case '/':
      find = "\n";
      break;

    case '*':
      find = "*/";
      break;

    default:
      return 0;
  }

  const char *search = p + 2;
  char *end = strstr(search, find);
  if (!end) {
    // FIXME: unclosed multiline comments don't update line_no
    return strlen(p);  // trailing comment goes to EOF
  }

  int len = end - p;
  if (p[1] == '/') {
    return len;
  }

  // count \n's for multiline comment
  char *newline = (char *) search;
  for (;;) {
    newline = memchr(newline, '\n', end - newline);
    if (!newline) {
      break;
    }
    ++(*line_no);
    ++newline;
  }
  return len + 2;
}

static char *consume_space(char *p, int *line_no) {
  for (;;) {
    char c = *p;
    if (!isspace(c)) {
      return p;
    } else if (c == '\n') {
      ++(*line_no);
    }
    ++p;
  }
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
  int len = consume_comment(p, &d->line_no);
  d->pending.len = len;
  d->line_after_pending = d->line_no;
  while (len) {
    p += len;
    p = consume_space(p, &d->line_no);
    len = consume_comment(p, &d->line_no);
  }

  // match real token
  eat_out eat = eat_token(p);
  d->next.type = eat.type;
  d->next.line_no = d->line_no;
  d->next.p = p;

  if (d->next.type == TOKEN_STRING) {
    // consume string (assume eat.len is zero)
    int litflag = 0;
    d->next.len = consume_string(p, &d->line_no, &litflag);
    if (litflag) {
      d->flag = FLAG__PENDING_T_BRACE;
    }
  } else {
    d->next.len = eat.len;
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
    d->pending.len = consume_comment(p, &d->line_after_pending);

    if (!d->pending.len) {
      return ERROR__INTERNAL;
    }
    return 0;
  }

  memcpy(out, &d->next, sizeof(token));

  // actually enact token
  if (d->depth == __STACK_SIZE) {
    return ERROR__STACK;
  }
  switch (out->type) {
    case TOKEN_SLASH:
      // consume this token as lookup can't know what it was
      if (has_value) {
        out->type = TOKEN_OP;
        out->len = consume_slash_op(out->p);
      } else {
        out->type = TOKEN_REGEXP;
        out->len = consume_slash_regexp(out->p);
      }
      d->next.len = out->len;
      break;

    case TOKEN_PAREN:
    case TOKEN_ARRAY:
    case TOKEN_BRACE:
      stack_inc(d, 0);
      break;

    case TOKEN_T_BRACE:
      stack_inc(d, 1);
      break;

    case TOKEN_CLOSE:
      if (!d->depth) {
        return ERROR__STACK;
      } else if (stack_dec(d)) {
        d->flag |= FLAG__RESUME_LIT;
      }
  }

  eat_next(d);
  return 0;
}

tokendef prsr_init_token(char *p) {
  tokendef d;
  bzero(&d, sizeof(d));
  d.buf = p;
  d.len = strlen(p);
  d.line_no = 1;

  d.pending.type = TOKEN_COMMENT;
  d.next.p = p;  // place next cursor

  // prsr state always points to next token
  eat_next(&d);
  return d;
}
