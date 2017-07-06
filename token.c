#include <ctype.h>
#include <string.h>
#include "token.h"

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

eat_out eat_token(tokendef *d, int slash_is_op) {
  // consume whitespace (look for newline, zero char)
  char c;
  for (;; ++d->curr) {
    c = peek_char(d, 0);
    if (c == '\n') {
      // newlines are magic in JS
      ++d->line_no;
      return (eat_out) {1, TOKEN_NEWLINE};
    } else if (!c) {
      // ending file, at top level, and non-empty statement
      return (eat_out) {0, TOKEN_EOF};  // end of file
    } else if (!isspace(c)) {
      break;
    }
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
      return (eat_out) {len, TOKEN_COMMENT};  // single line, done
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
    case ')':
      return (eat_out) {1, TOKEN_PAREN};

    case '[':
    case ']':
      return (eat_out) {1, TOKEN_ARRAY};

    case '{':
    case '}':
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
  // TODO: ` allows inner code inside ${ ... }
  if (c == '\'' || c == '"' || c == '`') {
    char start = c;
    int len = 0;
    while ((c = peek_char(d, ++len))) {
      // TODO: strchr for final, and check
      if (c == start) {
        ++len;
        break;
      } else if (c == '\\') {
        c = peek_char(d, ++len);
      }
      if (c == '\n') {
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

int prsr_next_token(tokendef *d, int slash_is_op, token *out) {
  out->p = NULL;
  out->line_no = d->line_no;

  eat_out eo = eat_token(d, slash_is_op);
  out->len = eo.len;
  out->type = eo.type;

  if (out->type < 0) {
    return d->curr;  // failed at this position
  }
  out->p = d->buf + d->curr;
  d->curr += out->len;
  return 0;
}
