
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "token.h"
#include "debug.h"

#include "consume.c"
#include "../tokens/helper.c"

#include "token-tables.h"

#ifndef EMSCRIPTEN
tokendef _td;
#endif



#define __CONSUME_CONTROL      1
#define __CONSUME_MAYBE_LABEL  2
#define __CONSUME_ARROW_BODY   3
#define __CONSUME_DOT_PROPERTY 4


int blep_token_init(char *p, int len) {
  bzero(td, sizeof(tokendef));

  td->at = p;
  td->end = p + len;
  td->line_no = 1;

  // depth is length of stack, check (td->depth - 1)
  td->stack[0].open = TOKEN_BLOCK;
  td->depth = 1;

  // sanity-check td->end is NULL
  if (len < 0 || td->end[0]) {
    debugf("got bad td->end");
    return ERROR__UNEXPECTED;
  }
  return 0;
}


static inline void blepi_consume_token(struct token *t, char *p, int *line_no) {
#define _ret(_len, _type) {t->type = _type; t->len = _len; return;};
#define _reth(_len, _type, _hash) {t->special = _hash; t->type = _type; t->len = _len; return;};
#define _inc_stack(_type) { \
      td->stack[td->depth].open = _type; \
      td->stack[td->depth].block_has_value = 0; \
      if (++td->depth == STACK_SIZE) { \
        debugf("hit stack upper limit"); \
        _ret(0, TOKEN_EOF); \
      } \
    }

  // always clear
  t->special = 0;

  const unsigned char initial = p[0];
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
            return 0;
          case '~':
            _reth(1, TOKEN_OP, MISC_BITNOT);
            return 0;
          case '!':
            if (c != '=') {
              _reth(1, TOKEN_OP, MISC_NOT);
              return 0;
            }
            break;
        }

        // nb. these are all allowed=1, so len=1 even though we're consuming more
        if (initial == '=' && c == '>') {
          _reth(2, TOKEN_OP, MISC_ARROW);  // arrow for arrow function
          return __CONSUME_ARROW_BODY;

        } else if (c == initial && (c == '+' || c == '-')) {
          // nb. we don't actually care which one this is

          // FIXME unrelated: can {} be determined as block - always follows { or right keyword?
          // TODO: determine pre or postfix here?

          _reth(2, TOKEN_OP, MISC_INCDEC);
          return 0;
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
          return 0;
        }
      } else if (c == '=') {
        // for 2 and 3-cases, allow = as suffix
        ++len;
      }

      _ret(len, TOKEN_OP);
      return 0;
    }

    case _LOOKUP__DOT:
      if (isdigit(p[1])) {
        _ret(blepi_consume_number(p), TOKEN_NUMBER);
        return 0;
      } else if (p[1] == '.' && p[2] == '.') {
        _reth(3, TOKEN_OP, MISC_SPREAD);
        return 0;
      }
      _reth(1, TOKEN_OP, MISC_DOT);
      return __CONSUME_DOT_PROPERTY;

    case _LOOKUP__Q:
      switch (p[1]) {
        case '.':
          _reth(2, TOKEN_OP, MISC_CHAIN);  // "?." operator
          return __CONSUME_DOT_PROPERTY;
        case '?':
          if (p[2] == '=') {
            _ret(3, TOKEN_OP);  // "??="
          } else {
            _ret(2, TOKEN_OP);  // "??"
          }
          return 0;
      }
      _inc_stack(TOKEN_TERNARY);
      _ret(1, TOKEN_TERNARY);
      return 0;

    case _LOOKUP__COMMA:
      _reth(1, TOKEN_OP, MISC_COMMA);
      return 0;

    case _LOOKUP__NUMBER:
      _ret(blepi_consume_number(p), TOKEN_NUMBER);
      return 0;

    case _LOOKUP__STRING:
      _ret(blepi_consume_basic_string(p, line_no), TOKEN_STRING);
      return 0;

    case _LOOKUP__SLASH:
      // js is dumb: slashes are ambiguous, so guess here. we're almost always right, but callers
      // can fix it later if we're, for example, being run through an esoteric validation suite.
      switch (prev->type) {
        case TOKEN_KEYWORD:  // reentry
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
          // ambig case: `import x from "foo" /foo/` is regexp
          _ret(1, TOKEN_OP);  // strings coeerce to numbers if divided (except import targets)

        case TOKEN_CLOSE:
          if (prev->p[0] == ':') {
            break;  // ternary is always regexp
          } else if (prev->p[0] == ']') {
            _ret(1, TOKEN_OP);  // always op
          }
          // ambig case: `if (1) /foo/` is regexp, `function foo() {} /foo/` is regexp
          _ret(1, TOKEN_OP);

        case TOKEN_SYMBOL:  // reentry
        case TOKEN_REGEXP:  // facepalm
        case TOKEN_NUMBER:
          _ret(1, TOKEN_OP);
      }

      _ret(blepi_consume_slash_regexp(p), TOKEN_REGEXP);

    case _LOOKUP__LIT:
      t->type = TOKEN_LIT;

      // don't hash if this is a property
      if (prev->special != MISC_DOT && prev->special != MISC_CHAIN) {
        len = consume_known_lit(p, &(t->special));

        char c = p[len];
        if (!lookup_symbol[c]) {
          t->len = len;

          // TODO: we have a keyword that we might care about
          //   * "class [sym] <extends {}> {}"
          //   * "for await ("
          //   * any other control start (consume to "(", mark as non-value)

          return;
        }
      }

      t->special = SPECIAL__PROPERTY;
      // fall-through

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

      t->len = len;

      if (td->stack[td->depth - 1].open == TOKEN_BLOCK) {
        // TODO: look for upcoming ":"

        // saved
        int buffer_line_no = td->line_no;

        char *after_p = p + len;
        int after_len = blepi_consume_void(after_p, &(td->line_no));
        after_p += after_len;

        if (after_p[0] != ':') {
          // TODO: don't throw away comment traversal (?)
          td->line_no = buffer_line_no;
          return;
        }

        // hooray!
        t->type = TOKEN_LABEL;

        td->buf_at = 0;
        td->buf_use = 1;

        struct token *next = td->buf + 0;
        next->len = 1;
        next->line_no = td->line_no;
        next->p = after_p;
        next->type = TOKEN_COLON;
        next->special = 0;
        next->vp = p + len;

        td->at = after_p + 1;
      }

      return;
    }

    case TOKEN_BRACE:
      if (prev->special == MISC_ARROW) {
        // `=> {}` is always a block
      } else if (prev->type == TOKEN_COLON && td->stack[td->depth - 1].open == TOKEN_BLOCK) {
        // `foo: {}` inside block is a label starting a block
      } else if (prev->type == TOKEN_CLOSE && prev->p[0] == ')' && prev->line_no == td->line_no) {
        // `() {}` on same line is always a block
        // TODO: this is just optimization over if/while/for opening ()'s
        // TODO: it can also be on multiple lines if precided by if/while/for/...
      } else {
        _inc_stack(TOKEN_BRACE);
        _ret(1, TOKEN_BRACE);
      }
      _inc_stack(TOKEN_BLOCK);
      _ret(1, TOKEN_BLOCK);

    case TOKEN_ARRAY:
    case TOKEN_PAREN:
      _inc_stack(op);
      _ret(1, op);

    case TOKEN_COLON:
      if (td->stack[td->depth - 1].open != TOKEN_TERNARY) {
        _ret(1, TOKEN_COLON);
      }
      // inside ternary stack, close it
      --td->depth;
      _reth(1, TOKEN_CLOSE, TOKEN_TERNARY);

    case TOKEN_CLOSE: {
      int update = td->depth - 1;
      if (!update) {
        debugf("got TOKEN_CLOSE with bad depth=%d", td->depth);
        _ret(0, TOKEN_EOF);
      }

      // normal non-string, close and record
      int prev = td->stack[update].open;
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
      _ret(len, TOKEN_STRING);
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
      _ret(1, TOKEN_SEMICOLON);

    case TOKEN_EOF:
    case _LOOKUP__SPACE:
    case _LOOKUP__NEWLINE:
      _ret(0, TOKEN_EOF);

    default:
#ifdef DEBUG
      if (op <= 0 || op > _TOKEN_MAX) {
        debugf("got invalid OP, returning EOF: %d initial=%d", op, initial);
        _ret(0, TOKEN_EOF);
      }
#endif
      _ret(1, op);
  }

#undef _ret
#undef _reth
#undef _inc_stack
}

int blep_token_next() {
  if (td->buf_use) {
    // we have stuff to send
    memcpy(&(td->curr), td->buf + td->buf_at, sizeof(struct token));

    td->buf_at++;
    if (td->buf_at == td->buf_use) {
      td->buf_at = 0;
      td->buf_use = 0;
    }

    return td->curr.type;
  }

  int void_len = blepi_consume_void(td->at, &(td->line_no));
  td->curr.vp = td->at;
  td->at += void_len;

  // save as we can't yet write p/line_no to `td->curr`
  char *p = td->at;
  int line_no = td->line_no;

  blepi_consume_token(&(td->curr), td->at, &(td->line_no));

  if (td->stack[td->depth - 1].open == TOKEN_BLOCK && td->curr.type == TOKEN_LIT && !(td->curr.special & SPECIAL__PROPERTY)) {
//    blepi_internal_check();
    debugf("got lit in block");
  }

  if (td->buf_use) {
    // TODO: gross, the consume fn sets this up so we can skip td->at
    td->curr.p = p;
    td->curr.line_no = line_no;
    return td->curr.type;
  }

  td->at += td->curr.len;

  td->curr.p = p;
  td->curr.line_no = line_no;


  // nb. this is all "can we consume successfully" stuff
  if (!td->curr.len) {
    if (td->at >= td->end) {
      return 0;  // got EOF for some reason
    }
    if (!td->depth || td->depth == STACK_SIZE) {
      debugf("stack err: %c (depth=%d)\n", td->at[0], td->depth);
      return ERROR__STACK;  // stack got too big/small
    }
    debugf("could not consume: %c (void=%ld)\n", td->at[0], td->curr.vp - td->curr.p);
    return ERROR__UNEXPECTED;
  }

  return td->curr.type;
}

