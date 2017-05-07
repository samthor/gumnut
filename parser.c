#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

typedef struct {
  char *buf;
  int curr;
  int len;
  int lineNo;
} def;

#define TYPE_FOO 1
#define TYPE_BAR 2

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

int eat_token(def *d) {

  // eat whitespace, ignored
  for (;;) {
    char c = d->buf[d->curr];
    if (!isspace(c)) {
      break;
    }
    if (c == '\n') {
      ++d->lineNo;
    }
    ++d->curr;
  }

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
    // FIXME: expect followons: square brackets, dot, comma, semicolon?
    // TODO: Some statements (keywords?) require a space after them; we should return whether a
    // token is followed by whitespace, for the high-level thing to work it out
    return len;  // found variable or symbol (or out of data)
  }

  if (isspace(c)) {
    return 0;
  }

  // TODO: strings?
  if (c == '\'' || c == '"' || c == '`') {
    char start = c;
    while ((c = peek_char(d, ++len))) {
      if (c == start) {
        ++len;
        break;
      } else if (c == '\\') {
        ++len;
      }
    }
    return len;
  }

  // semicolon - should we return this at all?
  if (c == ';') {
    return 1;
  }

  // control structures
  if (c == '{' || c == '}') {
    return 1;
  }

  // array notation
  if (c == '[' || c == ']') {
    return 1;
  }

  // brackets
  if (c == '(' || c == ')') {
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

    const char *search = (const char *) d->buf + d->curr + 2;
    char *at = strstr(search, find);
    if (at == NULL) {
      return d->len - d->curr;  // consumed whole string, not found
    }
    len = at - search + 2;
    if (next == '*') {
      // count \n's
      char *newline = (char *) search;
      while (newline < at) {
        newline = strchr(newline, '\n');
        if (!newline) {
          break;
        }
        ++d->lineNo;  // TODO: this places the comment on the final line, not at its start point
        ++newline;
      }
      len += 2;  // eat */
    }
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
    return len;
  }

  // dot notation (after number)
  if (c == '.') {
    // TODO: return symbol?
    return 1;
  }

  // ops: i.e., anything made up of =<& etc
  while (contains(ops, peek_char(d, len))) {
    ++len;
  }
  if (len > 0) {
    return len;  // found ops
  }

  // if not in [a-zA-Z...], then what are we?

  return len;
}

int consume(def *d) {

  for (;;) {
    int len = eat_token(d);
    if (len <= 0) {
      break;  // fail, otw we can't consume more
    }

    char *out = (char *) malloc(len + 1);
    strncpy(out, d->buf + d->curr, len);
    out[len] = 0;
    printf("%4d: `%s`\n", d->lineNo, out);

    d->curr += len;  // TODO: move to eat_token??
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
  d.lineNo = 1;

  consume(&d);
}
