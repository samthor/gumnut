#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include "parser.h"
#include "utils.h"

#define NEXT_EXPR     1   // looking for an expr, and next slash starts a regexp (not divide)
#define NEXT_ID       2   // next statement is an id, e.g. foo.await (await is id)
#define NEXT_RESTRICT 4   // we just had a continue/break/etc that forces ASI on newline
#define NEXT_NEWLINE  8   // was just a newline
#define NEXT_CONTROL  16  // we just had a control structure (if, while) that expects ()s
#define NEXT_EMPTY    32  // would the next statement be effectively empty
#define NEXT_AFTER_OP 64  // we just had an op (or "op-like" keyword)

#define STACK_CURLY     0   // is {}s
#define STACK_SQUARE    1   // is []s
#define STACK_ROUND     2   // is ()s
#define STACK_TYPEMASK  3   // mask for types
#define STACK_CONTROL   4   // brackets of a control structure, e.g. for/if/while (no ASIs)
#define STACK_STATEMENT 8   // the next {} under us is a statement (e.g., var x = class{};)
#define STACK_DO_PARENS 16  // the next-ish () is the do {} while (...); parens
#define STACK_CURLY_OBJ 32  // this {} is a dict/class

#define SIZE_STACK 256  // nb. 256 parses example GWT code correctly (ugh)

typedef struct {
  char *buf;
  int curr;
  int len;
  int line_no;
  int next_flags;  // state flags for parsing
  int depth;
  uint8_t stack[SIZE_STACK];
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

inline int modify_stack(def *d, int inc, int type) {
  if (inc) {
    if (d->depth == SIZE_STACK - 1) {
      return -1;
    }
    ++d->depth;
    d->stack[d->depth] = type;
    return 0;
  }

  uint8_t prev = d->stack[d->depth--];
  if (d->depth < 0) {
    return 1;
  } else if ((prev & STACK_TYPEMASK) != type) {
    return 2;
  }
  return 0;
}

inline int is_stack_set(def *d, int bit) {
  return d->stack[d->depth] & bit;
}

eat_out eat_raw_token(def *d) {
  // consume whitespace (look for newline, zero char)
  char c;
  for (;; ++d->curr) {
    c = peek_char(d, 0);
    // newlines are magic in JS
    if (c == '\n') {
      // after a restricted keyword, force ASI
      if ((d->next_flags & NEXT_RESTRICT) && !is_stack_set(d, STACK_CONTROL)) {
        d->next_flags = NEXT_EXPR | NEXT_EMPTY;
        return (eat_out) {0, PRSR_TYPE_ASI};
      }
      ++d->line_no;
      d->next_flags |= NEXT_NEWLINE;
      return (eat_out) {1, PRSR_TYPE_NEWLINE};
    } else if (!c) {
      // if the file is ending, and it would not be an empty statement, emit ASI
      if (!(d->next_flags & NEXT_EMPTY) && !is_stack_set(d, STACK_CONTROL)) {
        d->next_flags = NEXT_EXPR | NEXT_EMPTY;
        return (eat_out) {0, PRSR_TYPE_ASI};
      }
      return (eat_out) {0, PRSR_TYPE_EOF};  // end of file
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

  // semicolon
  if (c == ';') {
    d->next_flags = NEXT_EXPR | NEXT_EMPTY;
    return (eat_out) {1, PRSR_TYPE_SEMICOLON};
  }

  // array notation
  if (c == '[' || c == ']') {
    if (modify_stack(d, c == '[', STACK_SQUARE)) {
      return (eat_out) {-1, PRSR_TYPE_UNEXPECTED};
    }
    d->next_flags = (c == '[' ? NEXT_EXPR : 0);
    return (eat_out) {1, PRSR_TYPE_ARRAY};
  }

  // brackets
  if (c == '(') {
    // we followed an if/while etc, mark as a control group
    if (d->next_flags & NEXT_CONTROL) {
      d->stack[d->depth] |= STACK_CONTROL;
    }
    if (modify_stack(d, /* inc */ 1, STACK_ROUND)) {
      return (eat_out) {-1, PRSR_TYPE_UNEXPECTED};
    }
    d->next_flags = NEXT_EXPR | NEXT_EMPTY;
    return (eat_out) {1, PRSR_TYPE_BRACKET};
  } else if (c == ')') {
    if (modify_stack(d, /* dec */ 0, STACK_ROUND)) {
      return (eat_out) {-1, PRSR_TYPE_UNEXPECTED};
    }
    if (is_stack_set(d, STACK_CONTROL)) {
      // control, look for expressions next
      d->stack[d->depth] &= ~STACK_CONTROL;  // clear bit
      d->next_flags = NEXT_EXPR;  // end of control (), e.g. for (;;)
    } else {
      // non-control (e.g., (1)), expect ops next
      d->next_flags = 0;  // end of regular ()
    }

    // special-case do {} while ()
    if (is_stack_set(d, STACK_DO_PARENS)) {
      // nb. not really a newline, but effectively the same for ASI purposes
      d->next_flags = NEXT_NEWLINE;
    }
    return (eat_out) {1, PRSR_TYPE_BRACKET};
  }

  // misc
  if (c == ':' || c == '?' || c == ',') {
    d->next_flags = NEXT_EXPR;
    int type = PRSR_TYPE_ELISON;
    if (c == ':') {
      type = PRSR_TYPE_COLON;
    } else if (c == '?') {
      type = PRSR_TYPE_TERNARY;
    }
    return (eat_out) {1, type};
  }

  // ops: i.e., anything made up of =<& etc
  do {
    if (c == '/' && (d->next_flags & NEXT_EXPR)) {
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
    } else if (c == 'i' && next == 'n') {
      // this _could_ be 'in' or 'instanceof'
      if (isspace(peek_char(d, 2))) {
        len = 2;
      } else if (strstr(d->buf + d->curr + 2, "stanceof") && isspace(d->buf[d->curr + 10])) {
        len = 10;
      } else {
        break;
      }
      return (eat_out) {len, PRSR_TYPE_OP};
    } else {
      break;
    }

    while (len < allowed) {
      c = peek_char(d, ++len);
      if (c != start) {
        break;
      }
    }

    int asi = 0;
    int type = PRSR_TYPE_OP;
    if (start == '=' && c == '>') {
      type = PRSR_TYPE_ARROW;
      asi = 1;
      ++len;  // arrow function
    } else if (c == start && strchr("+-|&", start)) {
      asi = (start == '-' || start == '+');
      ++len;  // eat --, ++, || or &&: but no more
    } else if (c == '=') {
      // consume a suffix '=' (or whole ===, !==)
      c = peek_char(d, ++len);
      if (c == '=' && (start == '=' || start == '!')) {
        ++len;
      }
    }

    // insert ASI around ++, -- and =>
    if (asi && !is_stack_set(d, STACK_CONTROL) && (d->next_flags & NEXT_NEWLINE)) {
      // this causes the code to run 2x, but ASI is dumb anyway
      d->next_flags = NEXT_EXPR | NEXT_EMPTY;
      return (eat_out) {0, PRSR_TYPE_ASI};
    }

    // look for postfix/prefix ops
    // nb. this conditional is a bit ugly, matches --, ++
    if (asi && (start == '+' || start == '-')) {
      if (d->next_flags & NEXT_EXPR) {
        // prefix: do nothing
      } else {
        // postfix special-case: can have more ops after this
        d->next_flags = 0;
        return (eat_out) {len, type};
      }
    }

    d->next_flags = NEXT_EXPR | NEXT_AFTER_OP;
    return (eat_out) {len, type};
  } while (0);

  // dot notation that is NOT a number
  if (c == '.' && !isnum(next)) {
    if (next == '.' && peek_char(d, 2) == '.') {
      d->next_flags = NEXT_EXPR;
      return (eat_out) {3, PRSR_TYPE_SPREAD};  // found '...' operator
    }
    d->next_flags = NEXT_ID | NEXT_EXPR;  // nb. this allows "./foo/", but invalid anyway
    return (eat_out) {1, PRSR_TYPE_DOT};  // it's valid to say e.g., "foo . bar", so separate token
  }

  // nb. from here down, these are all statements that cause ASI
  // if we don't expect an expr, but there was a newline
  if ((d->next_flags & NEXT_NEWLINE) && !(d->next_flags & NEXT_EXPR) && !(d->next_flags & NEXT_EMPTY)) {
    d->next_flags = NEXT_EXPR | NEXT_EMPTY;
    return (eat_out) {0, PRSR_TYPE_ASI};
  }

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

  // control structures
  if (c == '{') {
    if (modify_stack(d, /* inc */ 1, STACK_CURLY)) {
      return (eat_out) {-1, PRSR_TYPE_UNEXPECTED};
    }
    d->next_flags = NEXT_EXPR | NEXT_EMPTY;
    return (eat_out) {1, PRSR_TYPE_CONTROL};
  } else if (c == '}') {
    // if we're ending a control structure and this would not be an empty statement, emit an ASI
    if (!(d->next_flags & NEXT_EMPTY)) {
      d->next_flags = NEXT_EXPR | NEXT_EMPTY;
      return (eat_out) {0, PRSR_TYPE_ASI};
    }
    if (modify_stack(d, /* dec */ 0, STACK_CURLY)) {
      return (eat_out) {-1, PRSR_TYPE_UNEXPECTED};
    }
    // if we expected a statement here, clear it
    if (d->stack[d->depth] & STACK_STATEMENT) {
      d->stack[d->depth] &= ~STACK_STATEMENT;
      d->next_flags = 0;  // function/class statement, not a normal hoist (or if/while/etc)
    } else {
      d->next_flags = NEXT_EXPR | NEXT_EMPTY;  // hoisted, so following is empty
    }
    return (eat_out) {1, PRSR_TYPE_CONTROL};
  }

  // this must be a regexp
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

    // check for do/while combo, reset if it's missing
    if (is_stack_set(d, STACK_DO_PARENS)) {
      if (len == 5 && !memcmp(s, "while", 5)) {
        // this is fine
      } else {
        d->stack[d->depth] &= ~STACK_DO_PARENS;
      }
    }

    // if we expect an ID, or this is not a keyword
    if (d->next_flags & NEXT_ID || !is_keyword(s, len)) {
      d->next_flags = 0;
      return (eat_out) {len, PRSR_TYPE_VAR};  // found keyword or var
    }

    int prev_flags = d->next_flags;
    d->next_flags = NEXT_EXPR | NEXT_AFTER_OP;

    if (len == 2 && !memcmp(s, "do", 2)) {
      // special-case "do"
      d->stack[d->depth] |= STACK_DO_PARENS;
    } else if (is_asi_keyword(s, len)) {
      // might force ASI (e.g., return\nfoo => return;\nfoo)
      d->next_flags |= NEXT_RESTRICT;
    } else if (is_control_keyword(s, len)) {
      // next bracketed expression is e.g. for() or if()
      d->next_flags |= NEXT_CONTROL;
    } else if ((prev_flags & NEXT_AFTER_OP) && is_hoist_keyword(s, len)) {
      // this is a class or function _statement_ (not a hoisted def)
      d->next_flags |= NEXT_CONTROL;
      d->stack[d->depth] |= STACK_STATEMENT;
      // TODO: if it's of a certain type, record that somehow...
    }

    return (eat_out) {len, PRSR_TYPE_KEYWORD};  // found keyword or var
  } while (0);

  // found nothing :(
  return (eat_out) {0, PRSR_TYPE_ERROR};
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
    out.whitespace_after = isspace(d->buf[d->curr + out.len]);  // might hit '\0', is fine
    d->curr += out.len;
  }

  return out;
}

int prsr_consume(char *buf, int (*fp)(token *)) {
  def d;
  memset(&d, 0, sizeof(d));
  d.buf = buf;
  d.len = strlen(buf);
  d.line_no = 1;
  d.next_flags = NEXT_EXPR | NEXT_EMPTY;

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

