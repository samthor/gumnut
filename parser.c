#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

typedef struct {
  char *buf;
  int curr;
  int len;
  int line_no;

  int slash_regexp;  // whether the next slash is a regexp
} def;

#define TYPE_FOO 1
#define TYPE_BAR 2

typedef struct {
  char *p;
  int len;
  int after_whitespace;
  int line_no;
} token;

typedef struct {
  struct node *cond; // wrapped conditional inside if(), while(), for() etc
  char *token;
  int type;
} node;

const char *ops = "><=!|&^+-/*%";

int contains(const char *s, char c) {
  if (!c) {
    return 0;
  }
  return strchr(s, c) != NULL;
}

char peek_char(def *d, int len) {
  int out = d->curr + len;
  if (out < d->len) {
    return d->buf[out];
  }
  return 0;
}

int isnum(char c) {
  return c >= '0' && c <= '9';
}

int eat_whitespace(def *d) {
  int len = 0;
  for (;;) {
    char c = d->buf[d->curr];
    if (!isspace(c)) {
      return len;
    } else if (c == '\n') {
      ++d->line_no;
    }
    ++d->curr;
    ++len;
  }
}

int eat_raw_token(def *d) {
  int len = 0;
  char c;
  for (;;) {
    c = peek_char(d, len);
    int valid = (len ? isalnum(c) : isalpha(c)) || c == '$' || c == '_' || c > 127;
    if (!valid) {
      break;
    }
    ++len;
  }
  if (len) {
    int regexp = 0;
    char *s = d->buf + d->curr;
    if (!strncmp(s, "await", len) || !strncmp(s, "yield", len)) {
      // TODO: there's probably more statements that change behavior like this
      regexp = 1;
    }

    d->slash_regexp = regexp;

    // FIXME: expect followons: square brackets, dot, comma, semicolon?
    return len;  // found variable or symbol (or out of data)
  }

  if (isspace(c)) {
    // FIXME: should never happen? consume whitespace?
    printf("panic: should never be isspace at eat_raw_token");
    return -1;
  }

  // consume strings
  if (c == '\'' || c == '"' || c == '`') {
    char start = c;
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
    d->slash_regexp = 0;
    return len;
  }

  // semicolon - should we return this at all?
  if (c == ';') {
    d->slash_regexp = 1;
    return 1;
  }

  // control structures
  if (c == '{' || c == '}') {
    d->slash_regexp = 1;
    return 1;
  }

  // array notation
  if (c == '[' || c == ']') {
    d->slash_regexp = (c == '[');
    return 1;
  }

  // brackets
  if (c == '(' || c == ')') {
    d->slash_regexp = (c == '(');
    return 1;
  }

  // comments (C99 and long)
  char next = peek_char(d, len+1);
  if (c == '/') {
    char *find = NULL;

    if (next == '/') {
      find = "\n";
    } else if (next == '*') {
      find = "*/";
    }

    if (find) {
      const char *search = (const char *) d->buf + d->curr + 2;
      char *at = strstr(search, find);
      if (at == NULL) {
        return d->len - d->curr;  // consumed whole string, not found
      }
      len = at - search + 2;  // add preamble

      if (next == '/') {
        return len;  // single line, done
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
      return len + 2;  // eat "*/"
    }
  }

  // this must be a regexp
  if (d->slash_regexp && c == '/') {
    int is_charexpr = 0;
    // FIXME: consume regexp until /, unless escaped or 'within' [] ([ within [] is ignored)

    c = next;
    ++len;
    for (;;) {
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
        ++d->line_no;  // TODO: should never happen, invalid
      }
      c = peek_char(d, ++len);
    }

    // match trailing flags
    while (isalnum(c)) {
      c = peek_char(d, ++len);
    }

    d->slash_regexp = 0;
    return len;
  }

  // number: "0", ".01", "0x100"
  if (isnum(c) || (c == '.' && isnum(next))) {
    c = next;
    ++len;
    for (;;) {
      if (!(isalnum(c) || c == '.')) {  // letters, dots, etc- misuse is invalid, so eat anyway
        break;
      }
      c = peek_char(d, ++len);
    }
    d->slash_regexp = 0;
    return len;
  }

  // dot notation (after number)
  if (c == '.') {
    // TODO: match symbol?
    d->slash_regexp = 1;
    return 1;
  }

  if (c == ',') {
    d->slash_regexp = 0;
    return 1;
  }

  if (c == ':' || c == '?') {
    d->slash_regexp = 1;
    return 1;
  }

  // ops: i.e., anything made up of =<& etc
  for (;;) {
    if (!contains(ops, c)) {
      if (len > 0) {
        return len;
      }
      break;
    }
    if (c == '=') {
      // only ='s from here on in
      while (c == '=') {
        c = peek_char(d, ++len);
      }
      d->slash_regexp = 1;
      return len;
    }
    c = peek_char(d, ++len);

    // FIXME: there's conditions around this-
    // 1. ops like ~, +, !, + ... they attach to the next token?
    // TODO: and some of them might mean slash_regexp (+ regexp? weird but valid).
  }

  if (c != 0) {
    // what are we?
    printf("panic: unknown: %c%c", c, next);
  }
  return len;
}

token eat_token(def *d) {
  token out;
  eat_whitespace(d);

  out.p = NULL;
  out.after_whitespace = 0;
  out.line_no = d->line_no;
  out.len = eat_raw_token(d);

  if (out.len > 0) {
    out.p = d->buf + d->curr;
    out.after_whitespace = isspace(d->curr + out.len);
    d->curr += out.len;
  }

  return out;
}

int consume(def *d) {

  for (;;) {
    token out = eat_token(d);

    if (!out.p) {
      if (d->curr < d->len) {
        printf("can't parse reminder:\n%s\n", d->buf + d->curr);
        return -1;
      }
      break;
    }
    printf("%4d: %.*s\n", out.line_no, out.len, out.p);
  }

  return 0;
}

// reads stdin into buf, reallocating as nessecary. returns strlen(buf) or < 0 for error.
int read_stdin(char **buf) {
  int pos = 0;
  int size = 1024;
  *buf = malloc(size);

  for (;;) {
    if (pos >= size - 1) {
      size *= 2;
      *buf = realloc(*buf, size);
    }
    if (!fgets(*buf + pos, size - pos, stdin)) {
      break;
    }
    pos += strlen(*buf + pos);
  }
  if (ferror(stdin)) {
    return -1;
  } 

  return pos;
}

int main() {
  char *buf;
  if (read_stdin(&buf) < 0) {
    return -1;
  }

  def d;
  d.buf = buf;
  d.len = strlen(d.buf);
  d.curr = 0;
  d.line_no = 1;
  d.slash_regexp = 1;

  consume(&d);
}
