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

typedef struct {
  int len;
  int type;
} eat_out;

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

eat_out eat_raw_token(def *d) {
  // consume whitespace (look for newline, zero char)
  char c;
  for (;; ++d->curr) {
    c = peek_char(d, 0);
    if (c == '\n') {
      // newlines are magic in JS
      ++d->line_no;
      return (eat_out) {1, PRSR_TYPE_NEWLINE};
    } else if (!c) {
      return (eat_out) {0, 0};  // end of file
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
      return (eat_out) {d->len - d->curr, PRSR_TYPE_COMMENT};  // consumed whole string, not found
    }
    int len = at - search + 2;  // add preamble

    if (next == '/') {
      return (eat_out) {len, PRSR_TYPE_COMMENT};  // single line, done
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
    return (eat_out) {len + 2, PRSR_TYPE_COMMENT};  // eat "*/"
  } while (0);

  // strings
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
    d->next_flags = 0;
    return (eat_out) {len, PRSR_TYPE_STRING};
  }

  // semicolon
  if (c == ';') {
    d->next_flags = NEXT_REGEXP;
    return (eat_out) {1, PRSR_TYPE_SEMICOLON};
  }

  // control structures
  if (c == '{' || c == '}') {
    d->next_flags = NEXT_REGEXP;
    return (eat_out) {1, PRSR_TYPE_CONTROL};
  }

  // array notation
  if (c == '[' || c == ']') {
    d->next_flags = (c == '[');
    return (eat_out) {1, PRSR_TYPE_ARRAY};
  }

  // brackets
  if (c == '(' || c == ')') {
    d->next_flags = (c == '(');
    return (eat_out) {1, PRSR_TYPE_BRACKET};
  }

  // misc
  if (c == ':' || c == '?' || c == ',') {
    d->next_flags = NEXT_REGEXP;
    int type = PRSR_TYPE_COMMA;
    if (c == ':') {
      type = PRSR_TYPE_COLON;
    } else if (c == '?') {
      type = PRSR_TYPE_TERNARY;
    }
    return (eat_out) {1, type};
  }

  // this must be a regexp
  if (d->next_flags && c == '/') {
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
        ++d->line_no;  // TODO: should never happen, invalid
      }
      c = peek_char(d, ++len);
    } while (c);

    // match trailing flags
    while (isalnum(c)) {
      c = peek_char(d, ++len);
    }

    d->next_flags = 0;
    return (eat_out) {len, PRSR_TYPE_REGEXP};
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
    d->next_flags = 0;
    return (eat_out) {len, PRSR_TYPE_NUMBER};
  }

  // dot notation (after number)
  if (c == '.') {
    if (next == '.' && peek_char(d, 2) == '.') {
      d->next_flags = NEXT_REGEXP;
      return (eat_out) {3, PRSR_TYPE_DOTDOTDOT};  // found '...' operator
    }
    d->next_flags = NEXT_ID;
    return (eat_out) {1, PRSR_TYPE_DOT};  // it's valid to say e.g., "foo . bar", so separate token
  }

  // ops: i.e., anything made up of =<& etc
  do {
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
    d->next_flags = NEXT_REGEXP;

    while (len < allowed) {
      c = peek_char(d, ++len);
      if (c != start) {
        break;
      }
    }

    int type = PRSR_TYPE_OP;
    if (start == '=' && c == '>') {
      type = PRSR_TYPE_ARROW;
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
    return (eat_out) {len, type};
  } while (0);

  // keywords or vars
  do {
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

    char *s = d->buf + d->curr;
    int type;

    // if we expect an ID, or this is not a keyword
    if (d->next_flags & NEXT_ID || !is_keyword(s, len)) {
      d->next_flags = 0;
      type = PRSR_TYPE_VAR;  // if we expect an ID, this is always VAR, not KEYWORD
    } else {
      d->next_flags = NEXT_REGEXP;  // regexp after keywords
      type = PRSR_TYPE_KEYWORD;
    }

    return (eat_out) {len, type};  // found keyword or var
  } while (0);

  // found nothing :(
  return (eat_out) {0, -1};
}

token eat_token(def *d) {
  token out;
  out.p = NULL;
  out.whitespace_after = 0;
  out.line_no = d->line_no;

  eat_out eo = eat_raw_token(d);  // after d->line_no, as this might increase it
  out.len = eo.len;
  out.type = eo.type;

  if (out.type > 0 && out.len >= 0) {
    out.p = d->buf + d->curr;
    out.whitespace_after = isspace(d->buf[d->curr + out.len]);
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
        return d.curr;
      }
      break;
    }

    fp(&out);
  }

  return 0;
}

