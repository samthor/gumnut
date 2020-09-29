
#include <strings.h>
#include <ctype.h>
#include "token.h"

#include "../prsr/src/tokens/helper.c"

#include "token-tables.h"

#ifndef EMSCRIPTEN
tokendef _td;
#endif

static int hint_type;


#ifdef DEBUG
#include <stdio.h>
#define debugf(...) fprintf(stderr, "!!! " __VA_ARGS__); fprintf(stderr, "\n")
#else
#define debugf (void)sizeof
#endif


int blep_token_init(char *p, int len) {
  bzero(td, sizeof(tokendef));

  td->at = p;
  td->end = p + len;
  td->line_no = 1;
  td->depth = 1;

  // sanity-check td->end is NULL
  if (len < 0 || td->end[0]) {
    return ERROR__UNEXPECTED;
  }

  // TODO: consume initial #! comment?

  return 0;
}

// consume regexp "/foobar/"
static inline int blepi_consume_slash_regexp(char *p) {
#ifdef DEBUG
  if (p[0] != '/') {
    return 0;
  }
#endif
  char *start = p;
  int is_charexpr = 0;

  while (++p < td->end) {
    switch (*p) {
      case '/':
        // nb. already known not to be a comment `//`
        if (is_charexpr) {
          continue;
        }

        // eat trailing flags
        do {
          ++p;
        } while (isalnum(*p));
        return p - start;

      case '\n':
        return p - start;

      case '[':
        is_charexpr = 1;
        continue;

      case ']':
        is_charexpr = 0;
        continue;

      case '\\':
        if (p[1] == '/' || p[1] == '[') {
          ++p;  // we can only escape these
        }
        continue;
    }
  }

  return p - start;
}

static inline int blepi_maybe_consume_alnum_group(char *p) {
  if (p[0] != '{') {
    return 0;
  }

  int len = 1;
  for (;;) {
    char c = p[len];
    ++len;

    if (c == '}') {
      return len;
    } else if (!isalnum(c)) {
      return ERROR__UNEXPECTED;
    }
  }
}

static inline int blepi_consume_basic_string(char *p, int *line_no) {
#ifdef DEBUG
  if (p[0] != '\'' && p[0] != '"') {
    return 0;
  }
#endif
  char *start = p;

  for (;;) {
    ++p;
    switch (*p) {
      case '\0':
        if (td->end == p) {
          return p - start;
        }
        continue;

      case '\n':
        // nb. not valid here
        ++(*line_no);
        continue;

      case '\\':
        if (p[1] == *start) {
          ++p;  // the only thing we care about escaping
        }
        continue;

      case '"':
      case '\'':
        if (*p == *start) {
          ++p;
          return p - start;
        }
        // do nothing, we found the other one
    }
  }
}

static inline int blepi_consume_template(char *p, int *line_no) {
  // p[0] will be ` or }
#ifdef DEBUG
  if (p[0] != '`' && p[0] != '}') {
    return 0;
  }
#endif
  char *start = p;

  for (;;) {
    ++p;
    switch (*p) {
      case '\0':
        if (td->end == p) {
          return p - start;
        }
        continue;

      case '\n':
        ++(*line_no);
        continue;

      case '\\':
        if (p[1] == '$' || p[1] == '`') {
          ++p;  // we can only escape these
        }
        continue;

      case '$':
        if (p[1] == '{') {
          return 2 + p - start;
        }
        continue;

      case '`':
        return 1 + p - start;
    }
  }
}

// consumes spaces/comments between tokens
static inline int blepi_consume_void(char *p, int *line_no) {
  int line_no_delta = 0;
  char *start = p;

  for (;;) {
    switch (*p) {
      case ' ':    // 32
      case '\t':   //  9
      case '\v':   // 11
      case '\f':   // 12
      case '\r':   // 13
        ++p;
        continue;

      case '\n':   // 10
        ++p;
        ++line_no_delta;
        continue;

      case '/': {  // 47
        char next = p[1];
        if (next == '/') {
          p = memchr(p, '\n', td->end - p);
          if (p == 0) {
            p = td->end;
          }
          continue;
        } else if (next != '*') {
          break;
        }

        // consuming multline
        p += 2;
        do {
          char c = *p;
          if (c == '*') {
            if (p[1] == '/') {
              p += 2;
              break;
            }
          } else if (c == '\n') {
            ++line_no_delta;
          }
        } while (++p < td->end);
        continue;
      }
    }

    break;  // unhandled, break below
  }

  (*line_no) += line_no_delta;
  return p - start;
}

// consumes number, assumes first char is valid (dot or digit)
static inline int blepi_consume_number(char *p) {
#ifdef DEBUG
  if (!(isdigit(p[0]) || (p[0] == '.' && isdigit(p[1])))) {
    return 0;
  }
#endif
  int len = 1;
  char c = p[1];
  for (;;) {
    if (!(isalnum(c) || c == '.' || c == '_')) {  // letters, dots, etc- misuse is invalid, so eat anyway
      break;
    }
    c = p[++len];
  }
  return len;
}

static inline void blepi_consume_token(struct token *t, char *p, int *line_no) {
#define _ret(_len, _type) {t->special = 0; t->type = _type; t->len = _len; return;};
#define _reth(_len, _type, _hash) {t->special = _hash; t->type = _type; t->len = _len; return;};
#define _inc_stack(_type) { \
      td->stack[td->depth] = _type; \
      if (++td->depth == STACK_SIZE) { \
        _ret(0, TOKEN_EOF); \
      } \
    }

  struct token *prev = &(td->curr);
  const char initial = p[0];
  int op = lookup_op[initial];
  int len = 0;

  switch (op) {
    case _LOOKUP__OP_1:
    case _LOOKUP__OP_2:
    case _LOOKUP__OP_3: {
      op &= 3;  // remove 32 bit, just use 1,2 bits
      len = 1;
      char c = p[len];
      while (len < op && c == initial) {
        c = p[++len];
      }

      if (len == 1) {
        // simple cases that are hashed
        switch (initial) {
          case '*':
            if (c == '=') {
              break;
            }
            _reth(1, TOKEN_OP, MISC_STAR);
          case '~':
            _reth(1, TOKEN_OP, MISC_BITNOT);
          case '!':
            if (c != '=') {
              _reth(1, TOKEN_OP, MISC_NOT);
            }
            break;
        }

        // nb. these are all allowed=1, so len=1 even though we're consuming more
        if (initial == '=' && c == '>') {
          _reth(2, TOKEN_OP, MISC_ARROW);  // arrow for arrow function
        } else if (c == initial && (c == '+' || c == '-')) {
          // nb. we don't actually care which one this is
          _reth(2, TOKEN_OP, MISC_INCDEC);
        } else if (c == initial && (c == '|' || c == '&')) {
          ++len;  // eat || or &&: but no more
        } else if (c == '=') {
          // consume a suffix '=' (or whole ===, !==)
          c = p[++len];
          if (c == '=' && (initial == '=' || initial == '!')) {
            ++len;
          }
        } else if (initial == '=') {
          // match equals specially
          _reth(1, TOKEN_OP, MISC_EQUALS);
        }
      } else if (c == '=') {
        // for 2 and 3-cases, allow = as suffix
        ++len;
      }

      _ret(len, TOKEN_OP);
    }

    case _LOOKUP__DOT:
      if (isdigit(p[1])) {
        _ret(blepi_consume_number(p), TOKEN_NUMBER);
      } else if (p[1] == '.' && p[2] == '.') {
        _reth(3, TOKEN_OP, MISC_SPREAD);
      }
      _reth(1, TOKEN_OP, MISC_DOT);

    case _LOOKUP__Q:
      switch (p[1]) {
        case '.':
          _reth(2, TOKEN_OP, MISC_CHAIN);  // "?." operator
        case '?':
          _ret(2, TOKEN_OP);
      }
      _inc_stack(TOKEN_TERNARY);
      _ret(1, TOKEN_TERNARY);

    case _LOOKUP__COMMA:
      _reth(1, TOKEN_OP, MISC_COMMA);

    case _LOOKUP__NUMBER:
      _ret(blepi_consume_number(p), TOKEN_NUMBER);

    case _LOOKUP__STRING:
      _ret(blepi_consume_basic_string(p, line_no), TOKEN_STRING);

    case _LOOKUP__SLASH:
      // js is dumb: slashes are ambiguous, so guess here. we're almost always right, but callers
      // can fix it later if we're, for example, being run through an esoteric validation suite.
      switch (prev->type) {
        case TOKEN_KEYWORD:  // only seen if modified by parser
        case TOKEN_LIT:
          if (prev->special & (_MASK_KEYWORD | _MASK_REL_OP | _MASK_UNARY_OP)) {
            break;
          }
          _ret(1, TOKEN_OP);

        case TOKEN_STRING:
          if (prev->p[prev->len - 1] == '{') {
            break;  // inner of template string starts expr
            // TODO: could check stack too
          }
          _ret(1, TOKEN_OP);  // strings coeerce to numbers if divided

        case TOKEN_CLOSE:
          if (prev->p[0] == ':') {
            break;  // must always be regexp
          }
          _ret(1, TOKEN_OP);

        case TOKEN_SYMBOL:  // only seen if modified by parser
        case TOKEN_REGEXP:  // facepalm
        case TOKEN_NUMBER:
          _ret(1, TOKEN_OP);
      }

      _ret(blepi_consume_slash_regexp(p), TOKEN_REGEXP);

    case _LOOKUP__LIT: {
      // don't hash if this is a property
      if (prev->special != MISC_DOT && prev->special != MISC_CHAIN) {
        t->special = 0;
        len = consume_known_lit(p, &(t->special));

        char c = p[len];
        if (!lookup_symbol[c]) {
          t->type = TOKEN_LIT;
          t->len = len;
          return;
        }
      }
      // fall-through
    }

    case _LOOKUP__SYMBOL: {
      char c = p[len];  // don't need to check this one, we know it's valid
      do {
        if (c != '\\') {
          c = p[++len];
          continue;
        }

        if (p[len + 1] != 'u') {
          break;
        }
        len += 2;

        // maybe consume {abcd} group, return length
        int group = blepi_maybe_consume_alnum_group(p + len);
        if (group < 0) {
          _ret(0, TOKEN_EOF);  // -1 if group doesn't close properly
        }
        len += group;
        c = p[len];
      } while (lookup_symbol[c]);

      _ret(len, TOKEN_LIT);
    }

    case TOKEN_BRACE:
    case TOKEN_ARRAY:
    case TOKEN_PAREN:
      _inc_stack(op);
      _ret(1, op);

    case TOKEN_COLON:
      if (td->stack[td->depth - 1] != TOKEN_TERNARY) {
        _ret(1, TOKEN_COLON);
      }
      // inside ternary stack, close it
      --td->depth;
      _reth(1, TOKEN_CLOSE, TOKEN_TERNARY);

    case TOKEN_CLOSE: {
      int update = td->depth - 1;
      if (!update) {
        _ret(0, TOKEN_EOF);
      }

      // normal non-string, close and record
      int prev = td->stack[update];
      if (prev != TOKEN_STRING) {
        td->depth = update;
        _reth(1, TOKEN_CLOSE, prev);
      }

      // restore into template stack
      int len = blepi_consume_template(p, line_no);
      int more = (p[len - 1] == '{');
      if (more) {
        // this was a template part like: }...${
        // so don't muck with the stack
        _ret(len, TOKEN_STRING);
      }
      td->depth = update;
      _reth(len, TOKEN_STRING, TOKEN_STRING);
    }

    case _LOOKUP__TEMPLATE: {
      int len = blepi_consume_template(p, line_no);
      int more = (p[len - 1] == '{');
      if (more) {
        _inc_stack(TOKEN_STRING);
      }
      _ret(len, TOKEN_STRING);
    }

    case _LOOKUP__SEMICOLON:
      if (prev->line_no == *line_no) {
        _reth(1, TOKEN_SEMICOLON, SPECIAL__SAMELINE);
      }
      _ret(1, TOKEN_SEMICOLON);

    case TOKEN_EOF:
    case _LOOKUP__SPACE:
    case _LOOKUP__NEWLINE:
      _ret(0, TOKEN_EOF);

    default:
#ifdef DEBUG
      if (op <= 0 || op > _TOKEN_MAX) {
        _ret(0, TOKEN_EOF);
      }
#endif
      _ret(1, op);
  }

#undef _ret
#undef _reth
#undef _inc_stack
}

int blep_token_update(int type) {
#ifdef DEBUG
  if (td->peek.p) {
    debugf("can't update once already peeked, request: %d", type);
    return ERROR__INTERNAL;
  }
#endif

  if (type == td->curr.type) {
    return 0;
  }

  if (type == TOKEN_REGEXP) {
#ifdef DEBUG
    if (!(td->curr.type == TOKEN_OP && td->curr.p[0] == '/')) {
      debugf("can't update non-slash to regexp: %d", td->curr.type);
      return ERROR__INTERNAL;
    }
#endif
    int len = blepi_consume_slash_regexp(td->curr.p);
    td->at += (len - 1);
    td->curr.len = len;
    td->curr.type = TOKEN_REGEXP;
    return 0;
  } else if (type == TOKEN_OP) {
#ifdef DEBUG
  if (td->curr.type != TOKEN_REGEXP) {
      debugf("can't update non-regexp to slash");
      return ERROR__INTERNAL;
  }
#endif
    // slash is always length=1
    td->at -= (td->curr.len + 1);
    td->curr.len = 1;
    td->curr.type = TOKEN_OP;
    return 0;
  }

  return ERROR__INTERNAL;
}

int blep_token_next() {
  if (td->peek.p) {
    memcpy(&td->curr, &td->peek, sizeof(struct token));
    td->peek.p = 0;
  } else {
    int void_len = blepi_consume_void(td->at, &(td->line_no));
    td->curr.vp = td->at;
    td->at += void_len;

    // save as we can't yet write p/line_no to `td->curr`
    char *p = td->at;
    char line_no = td->line_no;

    blepi_consume_token(&(td->curr), td->at, &(td->line_no));
    td->at += td->curr.len;

    td->curr.p = p;
    td->curr.line_no = line_no;
  }

  if (!td->curr.len) {
    if (td->at >= td->end) {
      return 0;
    }
    if (!td->depth || td->depth == STACK_SIZE) {
      debugf("stack err: %c (depth=%d)\n", td->at[0], td->depth);
      return ERROR__STACK;
    }
    debugf("could not consume: %c (void=%d)\n", td->at[0], td->curr.vp - td->curr.p);
    return ERROR__UNEXPECTED;
  }

  return td->curr.type;

  // TODO:
  // - resolve / ambig by using most likely sane choice, allow later slow-case fix
  // - for lookahead cases, we hope to answer in BUFFER_SIZE tokens; otherwise, we have to bump
  //   a specific token into the future (even though we can't use its result as part of the cache)
}

int blep_token_peek() {
#ifdef DEBUG
  if (td->peek.p) {
    // we need to allow this for a few cases
    debugf("peeked again: %d", td->peek.type);
    return td->peek.type;
  }
#endif
  int void_len = blepi_consume_void(td->at, &(td->line_no));
  td->peek.vp = td->at;
  td->at += void_len;

  td->peek.p = td->at;
  td->peek.line_no = td->line_no;
  blepi_consume_token(&(td->peek), td->at, &(td->line_no));
  td->at += td->peek.len;

  return td->peek.type;
}

int blep_token_set_restore() {
  if (td->restore__at) {
    return 0;
  }

  // clear peek and reset its contribution
  if (td->peek.p) {
    switch (td->peek.type) {
      case TOKEN_CLOSE:
        ++td->depth;
        break;

      case TOKEN_BRACE:
      case TOKEN_ARRAY:
      case TOKEN_PAREN:
      case TOKEN_TERNARY:
        --td->depth;
        break;
    }

    td->at = td->peek.vp;  // the cursor has been moved forward
    td->peek.p = 0;
  }

  memcpy(&(td->restore__curr), &(td->curr), sizeof(struct token));

  td->restore__line_no = td->line_no;
  td->restore__at = td->at;
  td->restore__depth = td->depth;
  return td->depth;
}

int blep_token_restore() {
  if (!td->restore__at) {
    return 0;
  }

  // TODO: set_restore and restore currently just move the top token back
  // but we could/should store a ring buffer of ~128 tokens for re-parsing

  memcpy(&(td->curr), &(td->restore__curr), sizeof(struct token));

  td->line_no = td->restore__line_no;
  td->at = td->restore__at;
  td->depth = td->restore__depth;

  td->restore__at = NULL;
  td->peek.p = NULL;

  return td->depth;
}

inline int blep_token_is_symbol_part(char c) {
  return lookup_symbol[c];
}