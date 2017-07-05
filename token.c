#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include "token.h"
#include "utils.h"

#define STACK__BRACE          0   // is {}s
#define STACK__ARRAY          1   // is []s
#define STACK__PAREN          2   // is ()s
#define STACK__OBJECT         4   // we are an object literal-ish
#define STACK__TYPEMASK       6   // mask for types
#define STACK__NEXT_STATEMENT 8   // the next {} under returns a statement (e.g., var x = class{};)
#define STACK__NEXT_OBJECT    16  // the next {} is unambiguously an object literal-ish
#define STACK__NEXT_CONTROL   32  // the next () under is a control (e.g., if (...))
#define STACK__NEXT_DO_PARENS 64  // the next () is do {} while () parens

#define FLAG__SLASH_IS_OP    1   // if we see a /, it's division (not regex)
#define FLAG__AFTER_OP       2   // we expect to see a variable next (had op or 'excepts' var)
#define FLAG__EXPECT_ID      4   // the next symbol is a name, not a keyword (e.g., foo.return)
#define FLAG__EXPECT_LABEL   8   // we just had a break/continue that can target a label
#define FLAG__MUST_ASI       16  // appears after parens owned by do/while
#define FLAG__RESTRICT       32  // just had a restricted keyword, newline generates ASI
#define FLAG__NEWLINE        64  // just after a newline

#define FLAG__MASK_BRACE_ASI (1 | 2 | 4 | 8 | 16)  // generate ASI before } in these cases

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

// increment or decrement the stack with a specific type: returns zero on success, -ve for error,
// +ve for the type expected in a decrement
int modify_stack(tokendef *d, int inc, int type) {
  if (inc) {
    if (!d->depth || d->depth == _TOKEN_STACK_SIZE - 1) {
      return -1;
    }
    ++d->depth;
    d->stack[d->depth] = type;
    return 0;
  }

  uint8_t prev = d->stack[d->depth--];
  if (d->depth < 1) {
    return -2;
  } else if ((prev & STACK__TYPEMASK) != type) {
    return prev & STACK__TYPEMASK;
  }
  return 0;
}

eat_out next_token(tokendef *d);

// find the next token type that isn't a comment, whitespace or newline: moves the passed tokendef
// forward, so pass a copy
int next_real_type(tokendef *d) {
  for (;;) {
    eat_out eo = next_token(d);
    if (eo.type <= 0 || eo.len < 0) {
      return -1;
    } else if (eo.type == TOKEN_COMMENT || eo.type == TOKEN_NEWLINE) {
      d->curr += eo.len;
      continue;
    }
    return eo.type;
  }
}

eat_out next_token(tokendef *d) {
  // consume whitespace (look for newline, zero char)
  char c;
  for (;; ++d->curr) {
    c = peek_char(d, 0);
    // newlines are magic in JS
    if (c == '\n') {
      // after a restricted keyword, force ASI
      if ((d->flags & FLAG__RESTRICT)) {
        d->flags = 0;
        return (eat_out) {0, TOKEN_ASI};
      }
      ++d->line_no;
      d->flags |= FLAG__NEWLINE;
      return (eat_out) {1, TOKEN_NEWLINE};
    } else if (!c) {
      // ending file, at top level, and non-empty statement
      if (d->depth == 1 && (d->flags & FLAG__MASK_BRACE_ASI)) {
        d->flags = 0;
        return (eat_out) {0, TOKEN_ASI};
      }
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
    case ';':
      return (eat_out) {1, TOKEN_SEMICOLON};

    case '?':
      d->flags |= FLAG__AFTER_OP;
      return (eat_out) {1, TOKEN_TERNARY};

    case ':':
      d->flags |= FLAG__AFTER_OP;
      return (eat_out) {1, TOKEN_COLON};

    case ',':
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
      if ((prev & STACK__OBJECT) && !(flags & FLAG__AFTER_OP)) {
        // inside an object, but not after an operation. this is probably foo() {}, i.e. a block
        // (if it's not, it's invalid anyway)
      } else if ((flags & FLAG__AFTER_OP) || (prev & STACK__TYPEMASK) || (prev & STACK__NEXT_OBJECT)) {
        d->stack[d->depth] |= STACK__OBJECT;
      }

      return (eat_out) {1, TOKEN_BRACE};

    case '}':
      // emit ASI if this ended an empty statement
      if ((d->stack[d->depth] & STACK__TYPEMASK) == STACK__BRACE) {
        if ((flags & FLAG__MASK_BRACE_ASI)) {
          d->flags = 0;
          return (eat_out) {0, TOKEN_ASI};
        }
      }

      // it's not clear whether we were an object, so allow it if we were (STACK__BRACE == 0)
      if (modify_stack(d, 0, STACK__BRACE | (d->stack[d->depth] & STACK__OBJECT))) {
        return (eat_out) {-1, -1};
      }

      // if that was the mandatory statement after a class/function, slash will be an op
      if (d->stack[d->depth] & STACK__NEXT_STATEMENT) {
        d->flags |= FLAG__SLASH_IS_OP;
      }

      // retain NEXT_DO_PARENS (as we might be at the "} while" ... bit)
      d->stack[d->depth] &= (STACK__TYPEMASK | STACK__NEXT_DO_PARENS);
      return (eat_out) {1, TOKEN_BRACE};

    case ']':
      if (modify_stack(d, 0, STACK__ARRAY)) {
        return (eat_out) {-1, -1};
      }
      d->flags |= FLAG__SLASH_IS_OP;

      d->stack[d->depth] &= STACK__TYPEMASK;
      return (eat_out) {1, TOKEN_ARRAY};

    case ')':
      if (modify_stack(d, 0, STACK__PAREN)) {
        return (eat_out) {-1, -1};
      }

      // if they were regular parens, slash is an op (the alternative is if() ...)
      if (!(d->stack[d->depth] & STACK__NEXT_CONTROL)) {
        d->flags |= FLAG__SLASH_IS_OP;
      }

      // special-case do {} while (), basically treat as newline for ASI purposes
      if (d->stack[d->depth] & STACK__NEXT_DO_PARENS) {
        d->flags |= (FLAG__NEWLINE | FLAG__MUST_ASI);
      }

      d->stack[d->depth] &= STACK__TYPEMASK;
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
      // arrow function: nb. if this is after a newline, it's invalid (doesn't generate ASI)
      return (eat_out) {2, TOKEN_ARROW};
    } else if (c == start && strchr("+-|&", start)) {
      if (start == '-' || start == '+') {
        // if we had a newline, and aren't in a control structure's brackets
        if ((flags & FLAG__NEWLINE) && !(d->stack[d->depth-1] & STACK__NEXT_CONTROL)) {
          d->flags = 0;
          return (eat_out) {0, TOKEN_ASI};
        }
        // if we're a postfix, allow more ops
        if (flags & FLAG__SLASH_IS_OP) {
          d->flags = FLAG__SLASH_IS_OP;  // removes FLAG__AFTER_OP
        }
      }
      ++len;  // eat --, ++, || or &&: but no more
    } else if (c == '=') {
      // consume a suffix '=' (or whole ===, !==)
      c = peek_char(d, ++len);
      if (c == '=' && (start == '=' || start == '!')) {
        ++len;
      }
    }

    if (!(flags & FLAG__SLASH_IS_OP)) {
      // if we get an unexpected op, but expect an ID, pass it forward: this allows "function*foo"
      d->flags |= (flags & FLAG__EXPECT_ID);
    }

    return (eat_out) {len, TOKEN_OP};
  } while (0);

  // dot notation
  if (c == '.') {
    if (next == '.' && peek_char(d, 2) == '.') {
      return (eat_out) {3, TOKEN_SPREAD};  // '...' operator
    }
    d->flags |= FLAG__EXPECT_ID;
    return (eat_out) {1, TOKEN_DOT};  // it's valid to say e.g., "foo . bar", so separate token
  }

  // nb. from here down, these are all statements that cause ASI
  // if we don't expect an expr, but there was a newline
  if ((flags & FLAG__NEWLINE) && (flags & FLAG__MASK_BRACE_ASI)) {
    d->flags = 0;
    return (eat_out) {0, TOKEN_ASI};
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

    // if we expect a literal (e.g. class Foo, function Bar) or id (foo.var), short-circuit
    if (flags & FLAG__EXPECT_ID) {
      d->flags |= FLAG__SLASH_IS_OP;
      return (eat_out) {len, TOKEN_SYMBOL};
    }

    // if we expect a label, short-circuit
    if (flags & FLAG__EXPECT_LABEL) {
      return (eat_out) {len, TOKEN_LABEL};
    }

    // special-case literal-ish, when on the left of :
    if ((d->stack[d->depth] & STACK__OBJECT) && !(flags & FLAG__AFTER_OP)) {
      if (len == 3 && (!memcmp(s, "get", 3) || !memcmp(s, "set", 3))) {
        // matched 'get' or 'set', return as keyword
        d->flags |= FLAG__EXPECT_ID;
        return (eat_out) {len, TOKEN_KEYWORD};
      }
      // otherwise, this is a literal name
      return (eat_out) {len, TOKEN_SYMBOL};
    }

    int expr_keyword = is_expr_keyword(s, len);

    // this is ambiguous: could be a label, symbol or var
    if (!(flags & FLAG__AFTER_OP) && !(d->stack[d->depth] & STACK__OBJECT) && !is_reserved_word(s, len)) {
      tokendef copy = *d;
      copy.curr += len;
      if (!expr_keyword) {
        // if this is await/yield etc, then slash is regxp: otherwise, assume op
        copy.flags |= FLAG__SLASH_IS_OP;
      }
      int type = next_real_type(&copy);
      if (type == TOKEN_COLON) {
        return (eat_out) {len, TOKEN_LABEL};
      } else if (type == TOKEN_STRING) {
        // found a backtick after a token: if we're await/yield etc, this is a keyword
        // FIXME: this should only be for backticks
        return (eat_out) {len, expr_keyword ? TOKEN_KEYWORD : TOKEN_SYMBOL};
      } else if (type == TOKEN_OP) {
        d->flags |= FLAG__SLASH_IS_OP;
        return (eat_out) {len, TOKEN_SYMBOL};
      }
    }

    // look for keywords which start expressions
    if (expr_keyword) {
      return (eat_out) {len, TOKEN_KEYWORD};
    }
    int expr = (d->stack[d->depth] & STACK__TYPEMASK) || (flags & FLAG__AFTER_OP);

    // matched hoistable class or function
    if (is_hoist_keyword(s, len)) {
      // look for function Foo, where Foo is a literal
      d->flags |= FLAG__EXPECT_ID;

      if (len == 5) {
        // the class {} is an object literal-ish
        d->stack[d->depth] |= STACK__NEXT_OBJECT;
      }
      if (expr) {
        // next {} builds a statement
        d->stack[d->depth] |= STACK__NEXT_STATEMENT;
      }

      return (eat_out) {len, TOKEN_KEYWORD};
    }

    // at this point, we must be an expression
    if (!expr) {
      if (d->stack[d->depth] & STACK__NEXT_DO_PARENS) {
        if (len != 5 || memcmp(s, "while", 5)) {
          // we only expect "while" here: could generate a syntax error
          d->stack[d->depth] &= ~STACK__NEXT_DO_PARENS;
        }
      }
      do {
        if (len == 2 && !memcmp(s, "do", 2)) {
          // got a "do", which isn't really a control, but implies it in future
          d->stack[d->depth] |= STACK__NEXT_DO_PARENS;
        } else if (is_control_keyword(s, len)) {
          // got an if/for/while etc, next ()'s are control
          d->stack[d->depth] |= STACK__NEXT_CONTROL;
        } else if (is_decl_keyword(s, len)) {
          // var/let/const must have a following ID, can't have new/await etc
          d->flags |= (FLAG__EXPECT_ID | FLAG__AFTER_OP);
        } else if (is_label_keyword(s, len)) {
          // break or continue, expects a label to follow
          d->flags |= FLAG__EXPECT_LABEL;
        } else if (is_asi_keyword(s, len)) {
          // this is just a type of keyword, but which generates an ASI
          d->flags |= (FLAG__AFTER_OP | FLAG__RESTRICT);
        } else if (is_keyword(s, len)) {
          // not explicitly an expression, match keywords
          d->flags |= FLAG__AFTER_OP;
        } else {
          // probably a symbol
          break;
        }
        return (eat_out) {len, TOKEN_KEYWORD};
      } while (0);
    }

    // nothing matched- must be a symbol
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
