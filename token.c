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
    return ERROR__STACK;
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
    return ERROR__STACK;
  }
  --d->depth;

  uint32_t *p = d->stack + (d->depth >> 5);
  uint32_t check = (uint32_t) 1 << (d->depth & 31);

  if (*p & check) {
    return 1;
  }
  return 0;
}

int eat_token(tokendef *d, eat_out *eat, int has_value) {
  int flag = d->flag;
  d->flag = 0;

#define _CONSUME(_len, _type) (eat->len = _len, eat->type = _type, 0);

  // look for EOF
  char c = peek_char(d, 0);
  if (!c) {
    _CONSUME(0, TOKEN_EOF);
    if (d->depth) {
      return ERROR__STACK;
    }
    return 0;
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
    char *end = strstr(search, find);
    if (end == NULL) {
      return _CONSUME(d->len - d->curr, TOKEN_COMMENT);  // comment to EOF
    }
    int len = end - search + 2;  // add preamble

    if (next == '/') {
      return _CONSUME(len, TOKEN_COMMENT);  // don't include newline
    }

    // count \n's for multiline comment
    char *newline = (char *) search;
    for (;;) {
      newline = memchr(newline, '\n', end - newline);
      if (!newline) {
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
    if (c == '/' && !has_value) {
      break;  // this is a regexp
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

static void consume_space_lookahead(tokendef *d) {
  d->lookahead_newline = 0;

  if (d->flag & FLAG__RESUME_LIT) {
    d->lookahead = -1;  // invalid lookahead, inside template literal
    return;
  }

  // always consume space chars
  for (char c;; ++d->curr) {
    c = d->buf[d->curr];
    if (!isspace(c)) {
      break;
    } else if (c == '\n') {
      ++d->line_no;
    }
  }

  if (d->lookahead <= d->curr) {
    return;  // nothing to do, we did this already
  }

  // move past all found comments (and whitespace) to find next char
  char *p = d->buf + d->curr;
  for (;;) {
    char c = *p;
    if (c != '/') {
      break;  // not a regex or comment
    }

    char *find = NULL;
    char next = p[1];
    if (next == '/') {
      find = "\n";
    } else if (next == '*') {
      find = "*/";
    } else {
      break;  // start of regex
    }

    const char *search = (const char *) p + 2;
    char *end = strstr(search, find);
    if (end == NULL) {
      d->lookahead = d->len;
      return;  // EOF
    }

    p = end + 1;
    if (next != '/') {
      if (!d->lookahead_newline) {
        // record if multiline comment had newline
        char *newline = memchr(search, '\n', end - search);
        if (newline) {
          d->lookahead_newline = 1;
        }
      }
      ++p;  // add both chars in "*/"
    }

    // move over whitespace
    while (isspace(*p)) {
      if (*p == '\n') {
        d->lookahead_newline = 1;
      }
      ++p;
    }
  }

  d->lookahead = p - d->buf;
}

int prsr_next_token(tokendef *d, token *out, int has_value) {
  out->line_no = d->line_no;  // set first, in case it changes

  eat_out eo;
  int ret = eat_token(d, &eo, has_value);
  if (ret) {
    return ret;
  } else if (eo.type < 0) {
    return ERROR__TOKEN;
  }

  out->p = d->buf + d->curr;
  out->len = eo.len;
  out->type = eo.type;
  d->curr += eo.len;

  // point to next token
  consume_space_lookahead(d);
  return 0;
}

tokendef prsr_init_token(char *p) {
  tokendef d;
  bzero(&d, sizeof(d));
  d.buf = p;
  d.len = strlen(p);
  d.line_no = 1;

  // prsr state always points to next token
  consume_space_lookahead(&d);
  return d;
}
