#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include "token.h"
#include "utils.h"

#define STACK__BRACE          0   // is {}s
#define STACK__ARRAY          1   // is []s
#define STACK__PAREN          2   // is ()s
#define STACK__TYPEMASK       3   // mask for types
#define STACK__OBJECT         4   // we are an object literal-ish
#define STACK__OBJECT_VALUE   8   // between : and ,
#define STACK__NEXT_STATEMENT 16  // the next {} under returns a statement (e.g., var x = class{};)
#define STACK__NEXT_OBJECT    32  // the next {} is unambiguously an object literal-ish
#define STACK__NEXT_CONTROL   64  // the next () under is a control (e.g., if (...))

#define FLAG__SLASH_IS_OP    1
#define FLAG__AFTER_OP       2
#define FLAG__EXPECT_ID      4
#define FLAG__EXPECT_LITERAL 8

typedef struct {
  int len;
  int type;
} eat_out;

char peek_char(tokendef *d, int len) {
  int out = d->curr + len;
  if (out < d->len) {
    return d->buf[out];
  }
  return 0;
}

int modify_stack(tokendef *d, int inc, int type) {
  if (inc) {
    if (d->depth == _TOKEN_STACK_SIZE - 1) {
      return -1;
    }
    ++d->depth;
    d->stack[d->depth] = type;
    return 0;
  }

  uint8_t prev = d->stack[d->depth--];
  if (d->depth < 0) {
    return -2;
  } else if ((prev & STACK__TYPEMASK) != type) {
    return prev & STACK__TYPEMASK;
  }
  return 0;
}

eat_out next_token(tokendef *d) {
  // consume whitespace (look for newline, zero char)
  char c;
  for (;; ++d->curr) {
    c = peek_char(d, 0);
    // newlines are magic in JS
    if (c == '\n') {
      ++d->line_no;
      return (eat_out) {1, TOKEN_NEWLINE};
    } else if (!c) {
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

  // save flags, reset to zero default
  int flags = d->flags;
  d->flags = 0;

  // various simple punctuation and all remaining brackets
  switch (c) {
#define _punct(_letter, _type) case _letter: return (eat_out) {1, _type};
    _punct(';', TOKEN_SEMICOLON);
    _punct('?', TOKEN_TERNARY);
#undef _punct

    case ':':
      if (d->stack[d->depth] & STACK__OBJECT) {
        d->stack[d->depth] |= STACK__OBJECT_VALUE;
      }
      return (eat_out) {1, TOKEN_COLON};

    case ',':
      d->stack[d->depth] &= ~STACK__OBJECT_VALUE;
      return (eat_out) {1, TOKEN_COLON};

    case '(':
      if (modify_stack(d, 1, STACK__PAREN)) {
        return (eat_out) {-1, -1};
      }
      return (eat_out) {1, TOKEN_PAREN};

    case '[':
      if (modify_stack(d, 1, STACK__ARRAY)) {
        return (eat_out) {-1, -1};
      }
      return (eat_out) {1, TOKEN_ARRAY};

    case '{':
      if (modify_stack(d, 1, STACK__BRACE)) {
        return (eat_out) {-1, -1};
      }

      // nb. there are some nuanced, orphaned {}'s that can be interpreted as either a dict or a
      // block. e.g.-
      //   { if (x) { } }
      // could be code with if, or a method if. but it's rare so assume 'orphaned' is block.
      int prev = d->stack[d->depth - 1];
      if ((flags & FLAG__AFTER_OP) || (prev & STACK__TYPEMASK) || (prev & STACK__NEXT_OBJECT)) {
        d->stack[d->depth] |= STACK__OBJECT;
      }

      return (eat_out) {1, TOKEN_BRACE};

    case '}':
      if (modify_stack(d, 0, STACK__BRACE)) {
        return (eat_out) {-1, -1};
      }

      // if that was the mandatory statement after a class/function, slash will be an op
      if (d->stack[d->depth] & STACK__NEXT_STATEMENT) {
        d->flags |= FLAG__SLASH_IS_OP;
      }
      d->stack[d->depth] &= STACK__TYPEMASK;

      return (eat_out) {1, TOKEN_BRACE};

    case ']':
      if (modify_stack(d, 0, STACK__ARRAY)) {
        return (eat_out) {-1, -1};
      }
      d->flags |= FLAG__SLASH_IS_OP;
      return (eat_out) {1, TOKEN_ARRAY};

    case ')':
      if (modify_stack(d, 0, STACK__PAREN)) {
        return (eat_out) {-1, -1};
      }

      // if they were regular parens, slash is an op (the alternative is if() ...)
      if (!(d->stack[d->depth] & STACK__NEXT_CONTROL)) {
        d->flags |= FLAG__SLASH_IS_OP;
      }

      return (eat_out) {1, TOKEN_PAREN};
  }

  // ops: i.e., anything made up of =<& etc, plus 'in' and 'instanceof'
  do {
    if (c == '/' && !(flags & FLAG__SLASH_IS_OP)) {
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
    } else {
      break;
    }

    d->flags |= FLAG__AFTER_OP;

    if (len > 0) {
      return (eat_out) {len, TOKEN_OP};
    }
    while (len < allowed) {
      c = peek_char(d, ++len);
      if (c != start) {
        break;
      }
    }

    if (start == '=' && c == '>') {
      // arrow function
      return (eat_out) {2, TOKEN_ARROW};
    } else if (c == start && strchr("+-|&", start)) {
      // FIXME: catch ++ or -- ?
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
    d->flags |= FLAG__SLASH_IS_OP;
    return (eat_out) {len, TOKEN_NUMBER};
  }

  // dot notation
  if (c == '.') {
    if (next == '.' && peek_char(d, 2) == '.') {
      return (eat_out) {3, TOKEN_SPREAD};  // '...' operator
    }
    d->flags |= FLAG__EXPECT_ID;
    return (eat_out) {1, TOKEN_DOT};  // it's valid to say e.g., "foo . bar", so separate token
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
    d->flags |= FLAG__SLASH_IS_OP;
    return (eat_out) {len, TOKEN_STRING};
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

    d->flags |= FLAG__SLASH_IS_OP;
    return (eat_out) {len, TOKEN_REGEXP};
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

    if (!len) {
      break;
    }
    char *s = d->buf + d->curr;

    // if we expect a literal (e.g. class Foo, function Bar), short-circuit
    if (flags & FLAG__EXPECT_LITERAL) {
      return (eat_out) {len, TOKEN_LITERAL};
    }

    // special-case literal-ish, when on the left of :
    if ((d->stack[d->depth] & STACK__OBJECT) && !(d->stack[d->depth] & STACK__OBJECT_VALUE)) {
      if (len == 3 && (c == 'g' || c == 's') && !memcmp(s+1, "et", 2)) {
        // matched 'get' or 'set', return as keyword
        d->flags |= FLAG__EXPECT_LITERAL;
        return (eat_out) {len, TOKEN_KEYWORD};
      }
      // otherwise, this is a literal name
      return (eat_out) {len, TOKEN_LITERAL};
    }

    // if we're not expecting a symbol, then look for keywords
    if (!(flags & FLAG__EXPECT_ID) && is_keyword(s, len)) {
      int hoist = is_hoist_keyword(s, len);
      if (hoist) {
        // look for function Foo, where Foo is a literal
        d->flags |= FLAG__EXPECT_LITERAL;
      }
      int candidate = (d->stack[d->depth] & STACK__TYPEMASK) || (flags & FLAG__AFTER_OP);
      if (candidate && hoist) {
        // got hoist keyword (function, class) and inside ([ or after op: next {} is a statement
        d->stack[d->depth] |= STACK__NEXT_STATEMENT;
      } else if (!(d->stack[d->depth] & STACK__TYPEMASK) && is_control_keyword(s, len)) {
        // got an if/for/while etc, next ()'s are control
        d->stack[d->depth] |= STACK__NEXT_CONTROL;
      }
      if (hoist && len == 5) {
        // the class {} is an object literal-ish
        d->stack[d->depth] |= STACK__NEXT_OBJECT;
      }
      return (eat_out) {len, TOKEN_KEYWORD};
    }

    // otherwise this is a symbol
    d->flags |= FLAG__SLASH_IS_OP;
    return (eat_out) {len, TOKEN_SYMBOL};
  } while (0);

  // found nothing :(
  return (eat_out) {0, -1};
}

int prsr_next_token(tokendef *d, token *out) {
  out->p = NULL;
  out->whitespace_after = 0;
  out->line_no = d->line_no;

  eat_out eo = next_token(d);  // after d->line_no, as this might increase it
  out->len = eo.len;
  out->type = eo.type;

  if (out->type <= 0 || out->len < 0) {
    if (d->curr < d->len) {
      return d->curr;
    }
    return -1;
  }

  out->p = d->buf + d->curr;
  d->curr += out->len;
  out->whitespace_after = isspace(d->buf[d->curr]);  // might hit '\0', is fine

  return 0;
}
