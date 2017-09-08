#include <ctype.h>
#include <string.h>
#include "token.h"

#define ERROR__STACK         -1
#define ERROR__STACK_INVALID -2

#define FLAG__PENDING_T_BRACE 1
#define FLAG__RESUME_LIT      2

typedef struct {
  int len;
  int type;
} eat_out;

static int isnum(char c) {
  return c >= '0' && c <= '9';
}

static char peek_char(tokendef *d, int len) {
  int out = d->curr + len;
  if (out < d->len) {
    return d->buf[out];
  }
  return 0;
}

static int stack_inc(tokendef *d, int type) {
  ++d->depth;
  d->stack[d->depth].type = type;
  if (d->depth == __STACK_SIZE - 1) {
    return ERROR__STACK;
  }
  return 0;
}

static int stack_dec(tokendef *d, int type) {
  if (d->stack[d->depth].type != type) {
    return ERROR__STACK_INVALID;
  }
  --d->depth;
  if (!d->depth) {
    return ERROR__STACK;
  }
  return 0;
}

eat_out eat_token(tokendef *d, token *out, int slash_is_op) {
  int flag = d->flag;
  d->flag = 0;

  // look for EOF
  char c = peek_char(d, 0);
  if (!c) {
    if (d->depth != 1) {
      out->invalid = 1;
    }
    return (eat_out) {0, TOKEN_EOF};
  }

  // flag states
  if (flag & FLAG__PENDING_T_BRACE) {
    stack_inc(d, TOKEN_T_BRACE);
    return (eat_out) {2, TOKEN_T_BRACE};
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
      return (eat_out) {d->len - d->curr, TOKEN_COMMENT};  // consumed whole string, not found
    }
    int len = at - search + 2;  // add preamble

    if (next == '/') {
      ++d->line_no;
      return (eat_out) {len + 1, TOKEN_COMMENT};  // single line including newline, done
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
    return (eat_out) {len + 2, TOKEN_COMMENT};  // eat "*/"
  } while (0);

  // unambiguous simple ascii characters
  switch (c) {
    case ';':
      return (eat_out) {1, TOKEN_SEMICOLON};

    case '?':
      return (eat_out) {1, TOKEN_TERNARY};

    case ':':
      return (eat_out) {1, TOKEN_COLON};

    case ',':
      return (eat_out) {1, TOKEN_COMMA};

    case '(':
      stack_inc(d, TOKEN_PAREN);
      return (eat_out) {1, TOKEN_PAREN};

    // FIXME: merge the three closing stack branches
    case ')': {
      int err = stack_dec(d, TOKEN_PAREN);
      if (err == ERROR__STACK_INVALID) {
        out->invalid = 1;
      }
      return (eat_out) {1, TOKEN_PAREN};
    }

    case '[':
      stack_inc(d, TOKEN_ARRAY);
      return (eat_out) {1, TOKEN_ARRAY};

    case ']': {
      int err = stack_dec(d, TOKEN_ARRAY);
      if (err == ERROR__STACK_INVALID) {
        out->invalid = 1;
      }
      return (eat_out) {1, TOKEN_ARRAY};
    }

    case '{':
      return (eat_out) {1, TOKEN_BRACE};

    case '}':
      if (d->stack[d->depth].type == TOKEN_T_BRACE) {
        stack_dec(d, TOKEN_T_BRACE);
        d->flag = FLAG__RESUME_LIT;
      } else {
        int err = stack_dec(d, TOKEN_BRACE);
        if (err == ERROR__STACK_INVALID) {
          out->invalid = 1;
        }
      }
      return (eat_out) {1, TOKEN_BRACE};

  }

  // ops: i.e., anything made up of =<& etc ('in' and 'instanceof' are ops, but here they are lit)
  do {
    if (c == '/' && !slash_is_op) {
      break;
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
      return (eat_out) {2, TOKEN_ARROW};
    } else if (c == start && strchr("+-|&", start)) {
      ++len;  // eat --, ++, || or &&: but no more
    } else if (c == '=') {
      // consume a suffix '=' (or whole ===, !==)
      c = peek_char(d, ++len);
      if (c == '=' && (start == '=' || start == '!')) {
        ++len;
      }
    }

    return (eat_out) {len, TOKEN_OP};
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
        d->flag = FLAG__PENDING_T_BRACE;
        break;
      }
      if (c == '\n') {
        // TODO: not allowed in quoted strings
        ++d->line_no;  // look for \n
      }
    }
    return (eat_out) {len, TOKEN_STRING};
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
    return (eat_out) {len, TOKEN_NUMBER};
  }

  // dot notation
  if (c == '.') {
    if (next == '.' && peek_char(d, 2) == '.') {
      return (eat_out) {3, TOKEN_SPREAD};  // '...' operator
    }
    return (eat_out) {1, TOKEN_DOT};  // it's valid to say e.g., "foo . bar", so separate token
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

    return (eat_out) {len, TOKEN_REGEXP};
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
    return (eat_out) {len, TOKEN_LIT};
  }

  // found nothing :(
  return (eat_out) {0, -1};
}

int prsr_next_token(tokendef *d, token *out) {
  for (char c;; ++d->curr) {
    c = d->buf[d->curr];
    if (c == '\n') {
      ++d->line_no;
    } else if (!isspace(c)) {
      break;
    }
  }

  int slash_is_op = 0;  // TODO

  out->p = NULL;
  out->line_no = d->line_no;
  out->invalid = 0;
  eat_out eo = eat_token(d, out, slash_is_op);
  out->len = eo.len;
  out->type = eo.type;

  if (d->depth == 0 || d->depth >= __STACK_SIZE - 1) {
    return -1;  // bad stack depth
  } else if (out->type < 0) {
    return d->curr;  // failed at this position
  }
  out->p = d->buf + d->curr;
  d->curr += out->len;
  return 0;
}

tokendef prsr_init_token(char *p) {
  tokendef d;
  bzero(&d, sizeof(d));
  d.buf = p;
  d.len = strlen(p);
  d.line_no = 1;
  d.depth = 1;
  return d;
}
