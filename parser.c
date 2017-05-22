#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "parser.h"

#define NEXT_REGEXP 1  // next slash starts a regexp (not division)
#define NEXT_ID     2  // next statement is an id, e.g. foo.await (await is id)

typedef struct {
  char *buf;
  int curr;
  int len;
  int line_no;
  int next_flags;  // state flags for parsing
} def;

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
    if (!isspace(c) || c == '\n') {
      return len;
    }
    ++d->curr;
    ++len;
  }
}

// nb. must start/end with space
const char keywords[] = " await break case catch class const continue debugger default delete do else enum export extends finally for function if implements import in instanceof interface let new package private protected public return static super switch throw try typeof var void while with yield ";

int is_keyword(char *s, int len) {
  if (len > 10 || len < 2) {
    return 0;  // no statements <2 or >10 ('instanceof')
  }
  for (int i = 0; i < len; ++i) {
    if (s[i] < 'a' || s[i] > 'z') {
      return 0;  // only a-z
    }
  }

  // TODO: do something better? strstr is probably fast D:
  // search for: space + candidate + space
  char cand[16];
  memcpy(cand+1, s, len);
  cand[0] = ' ';
  cand[len+1] = ' ';
  cand[len+2] = 0;

  return strstr(keywords, cand) != NULL;
}

int eat_raw_token(def *d) {
  int len = 0;
  char c = peek_char(d, len);
  if (!c) {
    return 0;
  }

  // newlines are magic in JS
  if (c == '\n') {
    ++d->line_no;
    return 1;
  }

  // whitespace
  if (isspace(c)) {
    // FIXME: should never happen? consume whitespace?
    printf("panic: should never be isspace at eat_raw_token");
    return -1;
  }

  // comments (C99 and long)
  char next = peek_char(d, len+1);
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
  } while (0);

  // strings
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
    d->next_flags = 0;
    return len;
  }

  // semicolon - should we return this at all?
  if (c == ';') {
    d->next_flags = NEXT_REGEXP;
    return 1;
  }

  // control structures
  if (c == '{' || c == '}') {
    d->next_flags = NEXT_REGEXP;
    return 1;
  }

  // array notation
  if (c == '[' || c == ']') {
    d->next_flags = (c == '[');
    return 1;
  }

  // brackets
  if (c == '(' || c == ')') {
    d->next_flags = (c == '(');
    return 1;
  }

  // misc
  if (c == ':' || c == '?' || c == ',') {
    d->next_flags = NEXT_REGEXP;
    return 1;
  }

  // this must be a regexp
  if (d->next_flags && c == '/') {
    int is_charexpr = 0;

    c = next;
    ++len;
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
        ++d->line_no;  // TODO: should never happen, invalid
      }
      c = peek_char(d, ++len);
    } while (c);

    // match trailing flags
    while (isalnum(c)) {
      c = peek_char(d, ++len);
    }

    d->next_flags = 0;
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
    d->next_flags = 0;
    return len;
  }

  // dot notation (after number)
  if (c == '.') {
    if (next == '.' && peek_char(d, len+2) == '.') {
      d->next_flags = NEXT_REGEXP;
      return 3;  // found '...' operator
    }
    d->next_flags = NEXT_ID;
    return 1;  // this doesn't match the symbol- it's valid to say e.g., "foo   .    bar".
  }

  // ops: i.e., anything made up of =<& etc
  do {
    const char start = c;
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
    d->next_flags = NEXT_REGEXP;

    while (len < allowed) {
      c = peek_char(d, ++len);
      if (c != start) {
        break;
      }
    }

    if (start == '=' && c == '>') {
      ++len;  // arrow function
    } else if (c == start && strchr("+-|&", start)) {
      ++len;  // eat --, ++, || or &&: but no more
    } else if (c == '=') {
      // consume a suffix '=' (or whole ===, !==)
      c = peek_char(d, ++len);
      if (c == '=' && (start == '=' || start == '!')) {
        ++len;
      }
    }
    return len;
  } while (0);

  // keywords or vars
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
    char *s = d->buf + d->curr;

    // if this is not an ID and is a keyword (e.g., await, export) then the next / is regexp
    if (!(d->next_flags & NEXT_ID) && is_keyword(s, len)) {
      d->next_flags = NEXT_REGEXP;
    } else {
      d->next_flags = 0;
    }

    return len;  // found varia
  }

  if (c != 0) {
    // what are we?
    printf("panic: unknown: %c %c\n", c, next);
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
    out.after_whitespace = isspace(d->buf[d->curr + out.len]);
    d->curr += out.len;
  }

  return out;
}

int prsr_consume(char *buf, int (*fp)(token *)) {
  def d;
  d.buf = buf;
  d.len = strlen(buf);
  d.curr = 0;
  d.line_no = 1;
  d.next_flags = NEXT_REGEXP;

  for (;;) {
    token out = eat_token(&d);

    if (!out.p) {
      if (d.curr < d.len) {
        printf("can't parse reminder:\n%s\n", d.buf + d.curr);
        return -1;
      }
      break;
    }

    fp(&out);
  }

  return 0;
}

