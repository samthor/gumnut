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

static char peek_char(tokendef *d, int len) {
  int out = d->curr + len;
  if (out < d->len) {
    return d->buf[out];
  }
  return 0;
}

static int stack_inc(tokendef *d, int t_brace) {
  if (d->depth == __STACK_SIZE) {
    return -1;
  }

  uint32_t *p = d->stack + (d->depth >> 5);
  uint32_t set = (uint32_t) 1 << (d->depth & 31);
  if (t_brace) {
    *p |= set;  // set bit
  } else {
    *p &= ~set; // clear bit
  }

  ++d->depth;
  return 0;
}

static int stack_dec(tokendef *d) {
  if (!d->depth) {
    return -1;
  }
  --d->depth;

  uint32_t *p = d->stack + (d->depth >> 5);
  uint32_t check = (uint32_t) 1 << (d->depth & 31);

  if (*p & check) {
    return 1;
  }
  return 0;
}

int eat_token(tokendef *d, eat_out *eat, tokenvalue tv) {
  int flag = d->flag;
  d->flag = 0;

#define _CONSUME(_len, _type) (eat->len = _len, eat->type = _type, 0);

  // look for EOF
  char c = peek_char(d, 0);
  if (!c) {
    _CONSUME(0, TOKEN_EOF);
    return d->depth > 0;
  }

  // flag states
  if (flag & FLAG__PENDING_T_BRACE) {
    _CONSUME(2, TOKEN_T_BRACE);
    return stack_inc(d, 1);
  } else if (flag & FLAG__RESUME_LIT) {
    goto resume_lit;
  }

  // comments (C99 and long)
  char next = peek_char(d, 1);
  do {
    char *find;
    if (c != '/') {
      break;
    } else if (next == '/') {
      find = "\n";
    } else if (next == '*') {
      find = "*/";
    } else {
      break;
    }

    const char *search = (const char *) d->buf + d->curr + 2;
    char *at = strstr(search, find);
    if (at == NULL) {
      return _CONSUME(d->len - d->curr, TOKEN_COMMENT);
    }
    int len = at - search + 2;  // add preamble

    if (next == '/') {
      ++d->line_no;
      return _CONSUME(len + 1, TOKEN_COMMENT);  // single line including newline, done
    }

    // count \n's
    char *newline = (char *) search;
    for (;;) {
      newline = strchr(newline, '\n');
      if (!newline || newline >= at) {
        break;
      }
      ++d->line_no;
      ++newline;
    }
    return _CONSUME(len + 2, TOKEN_COMMENT);  // eat "*/"
  } while (0);

  // unambiguous simple ascii characters
  switch (c) {
    case ';':
      return _CONSUME(1, TOKEN_SEMICOLON);

    case '?':
      return _CONSUME(1, TOKEN_TERNARY);

    case ':':
      return _CONSUME(1, TOKEN_COLON);

    case ',':
      return _CONSUME(1, TOKEN_COMMA);

    case '(':
      _CONSUME(1, TOKEN_PAREN);
      return stack_inc(d, 0);

    case '[':
      _CONSUME(1, TOKEN_ARRAY);
      return stack_inc(d, 0);

    case '{':
      _CONSUME(1, TOKEN_BRACE);
      return stack_inc(d, 0);

    case ')':
      _CONSUME(1, TOKEN_CLOSE);
      return stack_dec(d);

    case ']':
      _CONSUME(1, TOKEN_CLOSE);
      return stack_dec(d);

    case '}': {
      int ret = stack_dec(d);
      if (ret < 0) {
        return ret;
      } else if (ret > 0) {
        d->flag = FLAG__RESUME_LIT;
      }
      return _CONSUME(1, TOKEN_CLOSE);
    }
  }

  // ops: i.e., anything made up of =<& etc
  // note: 'in' and 'instanceof' are ops in most cases, but here they are lit
  do {
    if (c == '/') {
      int has_value = tv.check(tv.context);
      if (!has_value) {
        break;  // this is a regexp
      }
    }
    const char start = c;
    int len = 0;
    int allowed;  // how many ops of the same type we can safely consume

    if (strchr("=&|^~!%/+-", c)) {
      allowed = 1;
    } else if (c == '*' || c == '<') {
      allowed = 2;  // exponention operator **, or shift
    } else if (c == '>') {
      allowed = 3;  // right shift, or zero-fill right shift
    } else {
      break;
    }

    while (len < allowed) {
      c = peek_char(d, ++len);
      if (c != start) {
        break;
      }
    }

    if (start == '=' && c == '>') {
      // arrow function: nb. if this is after a newline, it's invalid (doesn't generate ASI), ignore
      return _CONSUME(2, TOKEN_ARROW);
    } else if (c == start && strchr("+-|&", start)) {
      ++len;  // eat --, ++, || or &&: but no more
    } else if (c == '=') {
      // consume a suffix '=' (or whole ===, !==)
      c = peek_char(d, ++len);
      if (c == '=' && (start == '=' || start == '!')) {
        ++len;
      }
    }

    return _CONSUME(len, TOKEN_OP);
  } while (0);

  // strings
  if (c == '\'' || c == '"' || c == '`') {
    char start = c;
    int len = 0;
    goto start_string;
resume_lit:
    start = '`';
    len = -1;
start_string:
    while ((c = peek_char(d, ++len))) {
      // TODO: strchr for final, and check
      if (c == start) {
        ++len;
        break;
      } else if (c == '\\') {
        c = peek_char(d, ++len);
      } else if (start == '`' && c == '$' && peek_char(d, len + 1) == '{') {
        d->flag = FLAG__PENDING_T_BRACE;  // next is "${"
        break;
      }
      if (c == '\n') {
        // TODO: not allowed in quoted strings
        ++d->line_no;  // look for \n
      }
    }
    return _CONSUME(len, TOKEN_STRING);
  }

  // number: "0", ".01", "0x100"
  if (isnum(c) || (c == '.' && isnum(next))) {
    int len = 1;
    c = next;
    for (;;) {
      if (!(isalnum(c) || c == '.')) {  // letters, dots, etc- misuse is invalid, so eat anyway
        break;
      }
      c = peek_char(d, ++len);
    }
    return _CONSUME(len, TOKEN_NUMBER);
  }

  // dot notation
  if (c == '.') {
    if (next == '.' && peek_char(d, 2) == '.') {
      return _CONSUME(3, TOKEN_SPREAD);  // '...' operator
    }
    return _CONSUME(1, TOKEN_DOT);  // it's valid to say e.g., "foo . bar", so separate token
  }

  // regexp
  if (c == '/') {
    int is_charexpr = 0;
    int len = 1;

    c = next;
    do {
      if (c == '[') {
        is_charexpr = 1;
      } else if (c == ']') {
        is_charexpr = 0;
      } else if (c == '\\') {
        c = peek_char(d, ++len);
      } else if (!is_charexpr && c == '/') {
        c = peek_char(d, ++len);
        break;
      }
      if (c == '\n') {
        ++d->line_no;  // nb. invalid in spec, but we allow here
      }
      c = peek_char(d, ++len);
    } while (c);

    // eat trailing flags
    while (isalnum(c)) {
      c = peek_char(d, ++len);
    }

    return _CONSUME(len, TOKEN_REGEXP);
  }

  // literals
  int len = 0;
  do {
    if (c == '\\') {
      ++len;  // don't care, eat whatever aferwards
      c = peek_char(d, ++len);
      if (c != '{') {
        continue;
      }
      while (c && c != '}') {
        c = peek_char(d, ++len);
      }
      ++len;
      continue;
    }

    // nb. `c < 0` == `((unsigned char) c) > 127`
    int valid = (len ? isalnum(c) : isalpha(c)) || c == '$' || c == '_' || c < 0;
    if (!valid) {
      break;
    }
    c = peek_char(d, ++len);
  } while (c);

  if (len) {
    // we don't care what this is, give to caller
    return _CONSUME(len, TOKEN_LIT);
  }

  // found nothing :(
  return _CONSUME(0, -1);
#undef _CONSUME
}

int prsr_next_token(tokendef *d, token *out, tokenvalue tv) {
  for (char c;; ++d->curr) {
    c = d->buf[d->curr];
    if (c == '\n') {
      ++d->line_no;
    } else if (!isspace(c)) {
      break;
    }
  }

  eat_out eo;
  int ret = eat_token(d, &eo, tv);
  if (ret < 0) {
    return ret;
  } else if (eo.type < 0) {
    return d->curr;  // failed at this position
  }

  out->p = d->buf + d->curr;
  out->len = eo.len;
  out->line_no = d->line_no;
  out->type = eo.type;
  out->invalid = (ret != 0);
  d->curr += eo.len;
  return 0;
}

tokendef prsr_init_token(char *p) {
  tokendef d;
  bzero(&d, sizeof(d));
  d.buf = p;
  d.len = strlen(p);
  d.line_no = 1;
  return d;
}
