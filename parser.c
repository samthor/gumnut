/*
 * Copyright 2017 Sam Thorogood. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */

#include <string.h>
#include <inttypes.h>
#include "token.h"
#include "parser.h"
#include "utils.h"

// states of parser: zero and everything else
#define STATE__ZERO             0
#define STATE__OBJECT           1
#define STATE__FUNCTION         2
#define STATE__CLASS            3
#define STATE__CLASSDEF         4
#define STATE__OPTIONAL_LABEL   5
#define STATE__EXPECT_ID        6
#define STATE__EXPECT_COLON     7
#define STATE__CONTROL          8
#define STATE__ARROW            9

#define FLAG__INITIAL        1
#define FLAG__VALUE          2
#define FLAG__WAS_ELSE       4
#define FLAG__CONTROL_PARENS 8

#define MODE__BRACE           0
#define MODE__VIRTUAL         1  // virtual {}, for control statements without braces
#define MODE__ARRAY           2
#define MODE__PAREN           3
#define MODE__CASE            4  // code inside "case ...:"
#define MODE__VALUE           5  // code inside "class Foo extends ... {" or "=> ..." (no brace)

#define CONTROL__DO           1
#define CONTROL__DO_WHILE     2
#define CONTROL__FOR          4
#define CONTROL__IF           8
#define CONTROL__TRY          16
#define CONTROL__CATCH        32

#define token_tc(token, t, c) (token->type == t && token->p[0] == c)
#define vtoken_tc(token, t, c) (token.type == t && token.p[0] == c)

int stack_inc(parserdef *p, uint8_t state) {
  ++p->curr;
  p->curr->state = state;
  p->curr->flag = 0;
  p->curr->value = 0;
  if (p->curr == p->stack + __STACK_SIZE - 1) {
    return ERROR__STACK;
  }
  return 0;
}

int stack_inc_zero(parserdef *p, uint8_t mode, uint8_t flag) {
  int err = stack_inc(p, STATE__ZERO);
  p->curr->flag = flag;
  p->curr->value = mode;
  return err;
}

int stack_dec(parserdef *p) {
  --p->curr;
  if (p->curr == p->stack) {
    return ERROR__STACK;
  }
  return 0;
}

int chunk_lookahead(parserdef *p, token *out) {
  int slash_is_op = (p->curr->state == STATE__ZERO) && (p->curr->flag & FLAG__VALUE);
  tokendef td = p->td;
  token placeholder;
  for (;;) {
    int ret = prsr_next_token(&td, slash_is_op, &placeholder);
    if (ret) {
      return -1;
    } else if (placeholder.type != TOKEN_COMMENT) {
      break;
    }
  }
  if (out) {
    *out = placeholder;
  }
  return placeholder.type;
}

int prsr_generates_asi(parserdef *p, token *out) {
  if (p->curr->state == STATE__CONTROL) {
    if (p->curr->value != CONTROL__DO_WHILE || out->type == TOKEN_COMMENT) {
      return 0;
    }
    // everything aside '(' generates an ASI after a do-while... that opens the conditional, but
    // otherwise just give up /shrug
    return !vtoken_tc(p->prev, TOKEN_PAREN, '(');
  }

  if (p->curr->state != STATE__ZERO) {
    return 0;
  }

  if (p->curr->value && p->curr->value != MODE__VIRTUAL && p->curr->value != MODE__VALUE) {
    return 0;
  }

  if (token_tc(out, TOKEN_BRACE, '}') || out->type == TOKEN_EOF) {
    return !(p->curr->flag & FLAG__INITIAL);
  }

  // nb. The rest of this method can be as slow as we like, as it only exists to punish the
  // terrible people who write JavaScript without semicolons.

  int effective_line = (out->type == TOKEN_COMMENT ? p->td.line_no : out->line_no);
  if (effective_line == p->prev.line_no || !p->prev.line_no) {
    return 0;  // no change
  }

  if (p->prev.type == TOKEN_KEYWORD && is_restrict_keyword(p->prev.p, p->prev.len)) {
    return -1;
  }

  switch (out->type) {
    case TOKEN_LIT:
      return (p->curr->flag & FLAG__VALUE) && !is_op_keyword(out->p, out->len);

    case TOKEN_OP:
      if ((p->curr->flag & FLAG__VALUE) && is_double_addsub(out->p, out->len)) {
        return -1;
      }
      return 0;

    case TOKEN_STRING:
    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
      return (p->curr->flag & FLAG__VALUE);
  }

  return 0;
}

// close the top-most 'empty' (i.e., still at initial) virtual block
int maybe_close_empty_virtual(parserdef *p) {
  parserstack *c = p->curr;
  if (c->state == STATE__ZERO && c->value == MODE__VIRTUAL && (c->flag & FLAG__INITIAL)) {
    return stack_dec(p);
  }
  return 0;
}

int chunk_inner(parserdef *p, token *out) {
  char *s = out->p;
  int len = out->len;

  switch (p->curr->state) {
    case STATE__ZERO:
      break;  // normal state, continue below

    case STATE__OPTIONAL_LABEL:
      stack_dec(p);
      if (out->type != TOKEN_LIT) {
        break;
      }
      out->type = TOKEN_LABEL;
      if (p->curr->state != STATE__ZERO) {
        return ERROR__INTERNAL;
      }
      p->curr->flag = 0;  // not initial, no value
      return 0;

    case STATE__EXPECT_ID:
      stack_dec(p);
      if (out->type != TOKEN_LIT) {
        break;  // our parent state is already not initial, not value
      }
      out->type = TOKEN_SYMBOL;
      return 0;

    case STATE__EXPECT_COLON:
      stack_dec(p);
      if (out->type != TOKEN_COLON) {
        break;  // just fail silently
      }
      return 0;

    case STATE__OBJECT:
      if (token_tc(out, TOKEN_BRACE, '}')) {
        return stack_dec(p);
      }

      // { [(get|set)] blah() {}, blah: function() }
      return ERROR__TODO;

    case STATE__FUNCTION:
      // look for end brace of function
      if (vtoken_tc(p->prev, TOKEN_BRACE, '}')) {
        stack_dec(p);
        maybe_close_empty_virtual(p);
        return ERROR__RETRY;
      }

      if (out->type == TOKEN_LIT) {
        out->type = TOKEN_SYMBOL;  // name of function
        return 0;
      } else if (out->type == TOKEN_OP) {
        // FIXME: enforce? order: function [*][foo]
        if (out->len == 1 && out->p[0] == '*' && p->prev.type == TOKEN_KEYWORD) {
          return 0;  // the '*' from a generator, immediately following 'function'
        }
      } else if (token_tc(out, TOKEN_PAREN, '(')) {
        return stack_inc_zero(p, MODE__PAREN, 0);
      } else if (token_tc(out, TOKEN_BRACE, '{')) {
        return stack_inc_zero(p, MODE__BRACE, FLAG__INITIAL);
      } else {
        out->invalid = 1;
        return 0;  // something went wrong
      }
      stack_dec(p);
      break;

    case STATE__CLASS:
      // look for end brace of class
      if (vtoken_tc(p->prev, TOKEN_BRACE, '}')) {
        stack_dec(p);
        maybe_close_empty_virtual(p);
        return ERROR__RETRY;
      }

      if (out->type == TOKEN_LIT) {
        if (out->len == 7 && !memcmp(out->p, "extends", 7)) {
          out->type = TOKEN_KEYWORD;
          // TODO: push 'single var without ops' on stack
          return ERROR__TODO;
        }
        out->type = TOKEN_SYMBOL;  // name of class
        return 0;
      } else if (token_tc(out, TOKEN_BRACE, '{')) {
        return stack_inc(p, STATE__CLASSDEF);
      } else {
        out->invalid = 1;
        return 0;  // something went wrong
      }
      stack_dec(p);
      break;

    case STATE__CLASSDEF:
      if (token_tc(out, TOKEN_BRACE, '}')) {
        return stack_dec(p);
      }
      if (out->type == TOKEN_LIT) {
        if (is_getset(out->p, out->len) ||
            (out->len == 6 && !memcmp(out->p, "static", 6))) {
          // FIXME: these should always be in order: [get|set] static
          // e.g. "static get get" is a static getter for "get"
          out->type = TOKEN_KEYWORD;  // initial keyword, any order is fine
          return 0;
        }
      } else if (out->type == TOKEN_OP) {
        // ok, could be *
      } else {
        out->invalid = 1;
        return 0;  // something went wrong
      }

      // push function: it looks for *, a starting literal, () and body
      stack_inc(p, STATE__FUNCTION);
      return ERROR__RETRY;

    case STATE__ARROW:
      stack_dec(p);
      if (token_tc(out, TOKEN_BRACE, '{')) {
        stack_inc_zero(p, MODE__BRACE, FLAG__INITIAL);
        return 0;
      }
      stack_inc_zero(p, MODE__VALUE, 0);
      break;

    case STATE__CONTROL: {
      int flag = p->curr->flag;
      p->curr->flag = 0;

      // look for 'else if'
      if (flag & FLAG__WAS_ELSE) {
        if (out->type == TOKEN_LIT && out->len == 2 && !memcmp(out->p, "if", 2)) {
          // check specifically for 'else if' and tweak current state: do this because else if
          // shouldn't increment depth, it's just another branch (unlike e.g., 'else while', which
          // does increase depth)
          p->curr->value = CONTROL__IF;
          p->curr->flag = FLAG__INITIAL;
          out->type = TOKEN_KEYWORD;
          return 0;
        }
        goto opener;  // skip other conditions, must open now
      }

      // catch trailing parens or end-of-control: on a do/while only
      if (p->curr->value == CONTROL__DO_WHILE) {
        if (out->type == TOKEN_SEMICOLON) {
          return stack_dec(p);  // all done
        }
        if (!token_tc(out, TOKEN_PAREN, '(')) {
          return ERROR__INTERNAL;  // ASI insertion should prevent this from happening
        }
        return stack_inc_zero(p, MODE__PAREN, 0);  // nb. same as next conditional
      }

      // catch opening parens: initial, not a do/while, and got a '('
      if ((flag & FLAG__CONTROL_PARENS) && token_tc(out, TOKEN_PAREN, '(')) {
        int pflag = (p->curr->value == CONTROL__FOR ? FLAG__INITIAL : 0);
        return stack_inc_zero(p, MODE__PAREN, pflag);
      }

      // handle previously closing } or virtual statements
      if (vtoken_tc(p->prev, TOKEN_BRACE, '}') || p->prev.type == TOKEN_SEMICOLON) {
        if (p->curr->value == CONTROL__DO) {
          // after closing the inner do-while statement, must see 'while'
          if (out->type == TOKEN_LIT && out->len == 5 && !memcmp(out->p, "while", 5)) {
            p->curr->value = CONTROL__DO_WHILE;
            out->type = TOKEN_KEYWORD;
            return 0;
          }
          // otherwise, this is invalid: fall-through to retry code
        } else if (out->type == TOKEN_LIT && is_trailing_control_keyword(out->p, out->len)) {
          // look for valid trailers
          char opt = out->p[0];
          if ((p->curr->value == CONTROL__IF && opt == 'e') ||
              (p->curr->value == CONTROL__TRY && (opt == 'f' || opt == 'c')) ||
              (p->curr->value == CONTROL__CATCH && opt == 'f')) {
            if (opt == 'c') {
              p->curr->flag = FLAG__CONTROL_PARENS;
              p->curr->value = CONTROL__CATCH;
            } else {
              p->curr->value = 0;  // else/finally don't trigger parens
            }
            out->type = TOKEN_KEYWORD;
            return 0;
          }
          // this is a trailer, but _we_ don't support it: retry below
        }

        stack_dec(p);  // finished control
        if (p->curr->state == STATE__ZERO && p->curr->value == MODE__VIRTUAL) {
          stack_dec(p);  // we also finished the parent virtual
        }
        return ERROR__RETRY;
      }

    opener:
      // catch opening {'s - or 'other' implies virtual statement
      if (token_tc(out, TOKEN_BRACE, '{')) {
        return stack_inc_zero(p, MODE__BRACE, FLAG__INITIAL);  // real brace
      }
      stack_inc_zero(p, MODE__VIRTUAL, FLAG__INITIAL);
      break;
    }

    default:
      return ERROR__INTERNAL;
  }

  int flag = p->curr->flag;
  p->curr->flag = 0;

  if (out->type == TOKEN_LIT && is_op_keyword(s, len)) {
    // replace 'in' and 'instanceof' with op
    out->type = TOKEN_OP;
  }

  switch (out->type) {
    case TOKEN_EOF:
      if (p->curr > p->stack + 1) {
        return ERROR__UNEXPECTED;
      }
      return 0;

    case TOKEN_COLON:
      if (p->curr->value == MODE__CASE) {
        stack_dec(p);
      }
      return 0;

    case TOKEN_COMMA:
      if (p->curr->value == MODE__VALUE) {
        stack_dec(p);
      }
      return 0;

    case TOKEN_SPREAD:
    case TOKEN_TERNARY:
    case TOKEN_OP:
      return 0;

    case TOKEN_ARROW:
      p->curr->flag = FLAG__VALUE;
      return stack_inc(p, STATE__ARROW);

    case TOKEN_SEMICOLON:
      switch (p->curr->value) {
        // VALUE and CASE aren't supposed to end here, but release for sanity
        case MODE__VALUE:
        case MODE__CASE:
        case MODE__VIRTUAL:
          stack_dec(p);
      }
      if (p->curr->state == STATE__ZERO) {
        p->curr->flag = FLAG__INITIAL;
      }
      return 0;

    case TOKEN_STRING:
    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
      p->curr->flag = FLAG__VALUE;
      return 0;

    case TOKEN_PAREN:
      if (s[0] == '(') {
        return stack_inc_zero(p, MODE__PAREN, 0);
      } else if (p->curr->value == MODE__PAREN) {
        return stack_dec(p);
      }
      return ERROR__UNEXPECTED;

    case TOKEN_ARRAY:
      if (s[0] == '[') {
        return stack_inc_zero(p, MODE__ARRAY, 0);
      } else if (p->curr->value == MODE__ARRAY) {
        return stack_dec(p);
      }
      return ERROR__UNEXPECTED;

    case TOKEN_BRACE:
      if (s[0] == '{') {
        // this is a block if we're the first expr (orphaned block)
        if (flag & FLAG__INITIAL) {
          p->curr->flag = FLAG__INITIAL;  // reset for after
          return stack_inc_zero(p, MODE__BRACE, FLAG__INITIAL);
        }
        return stack_inc(p, STATE__OBJECT);
      } else if (p->curr->value == MODE__BRACE && p->curr > p->stack) {
        return stack_dec(p);
      }
      return ERROR__UNEXPECTED;

    case TOKEN_DOT:
      if (!(flag & FLAG__VALUE)) {
        // unexpected
      }
      return stack_inc(p, STATE__EXPECT_ID);

    case TOKEN_LIT:
      break;  // continues excitement below

    default:
      return ERROR__TODO;
  }

  do {
    if (!is_begin_expr_keyword(s, len)) {
      break;
    }
    if (s[0] == 'l') {
      // special-case for 'let'
      if (!(flag & FLAG__INITIAL)) {
        break;  // after initial, it's a symbol
      }
      token next;
      chunk_lookahead(p, &next);
      if (next.type == TOKEN_ARRAY && next.p[0] == '[') {
        return 0;  // let[ is ambiguous, leave as TOKEN_LIT
      }
    }

    out->type = TOKEN_KEYWORD;
    return 0;
  } while(0);

  // found keyword which is an expr: e.g., 'await foo'
  if (is_expr_keyword(s, len)) {
    out->type = TOKEN_KEYWORD;
    return 0;
  }

  // look for hoistable keyword: function or class
  if (is_hoist_keyword(s, len)) {
    int state = (s[0] == 'f' ? STATE__FUNCTION : STATE__CLASS);
    out->type = TOKEN_KEYWORD;

    if ((p->curr->value == MODE__VIRTUAL || p->curr->value == MODE__BRACE) &&
        (flag & FLAG__INITIAL)) {
      // if a function is the only thing in a virtual, it's effectively a statement
      // ... even though interpreters actually have to hoist them (ugh)
      // inside a brace, initial keyword: this is a hoist
      p->curr->flag |= FLAG__INITIAL;
    } else {
      // otherwise, this is a statement and generates a value
      p->curr->flag |= FLAG__VALUE;
    }
    stack_inc(p, state);
    return 0;
  }

  // from here down, if we see a 

  do {
    if ((p->curr->value && p->curr->value != MODE__VIRTUAL) || !(flag & FLAG__INITIAL)) {
      // TODO: this looks for keywords that we match in "normal" mode, and marks them as keywords,
      // even though they shouldn't do anything (they're unexpected).
      // we need a better way to categorize these
      if (is_control_keyword(s, len) ||
          is_trailing_control_keyword(s, len) ||
          is_label_keyword(s, len) ||
          is_isolated_keyword(s, len) ||
          is_labellike_keyword(s, len)) {
        out->type = TOKEN_KEYWORD;
        out->invalid = 1;
        return 0;
      }
      break;
    }

    parserstack *was = p->curr;
    if (is_control_keyword(s, len)) {
      stack_inc(p, STATE__CONTROL);
      switch (s[0]) {
        case 'd':  // do
          p->curr->value = CONTROL__DO;
          break;
        case 't':  // try
          p->curr->value = CONTROL__TRY;
          break;
        case 'f':  // for
          p->curr->value = CONTROL__FOR;
          p->curr->flag = FLAG__CONTROL_PARENS;
          break;
        case 'i':  // if
          p->curr->value = CONTROL__IF;
          // fall-through
        default:
          p->curr->flag = FLAG__CONTROL_PARENS;
      }
    } else if (is_trailing_control_keyword(s, len)) {
      // e.g. 'else', only valid as a trailer: treat as control anyway
      stack_inc(p, STATE__CONTROL);
      switch (s[0]) {
        case 'c':  // catch
          p->curr->value = CONTROL__CATCH;
          p->curr->flag = FLAG__CONTROL_PARENS;
          break;
        case 'e':  // else
          p->curr->flag |= FLAG__WAS_ELSE;
          break;
      }
      out->invalid = 1;
    } else if (is_label_keyword(s, len)) {
      // look for next optional label ('break foo;')
      stack_inc(p, STATE__OPTIONAL_LABEL);
    } else if (is_isolated_keyword(s, len)) {
      // matches 'debugger'
    } else if (is_labellike_keyword(s, len)) {
      if (s[0] == 'd') {
        stack_inc(p, STATE__EXPECT_COLON);
      } else {
        stack_inc_zero(p, MODE__CASE, 0);
      }
    } else {
      break;
    }

    was->flag = FLAG__INITIAL;  // reset whatever it was to be initial
    out->type = TOKEN_KEYWORD;
    return 0;
  } while (0);

  // lookahead cases
  token lookahead;
  lookahead.p = NULL;

  // look for potential labels
  if (flag & FLAG__INITIAL) {
    // TODO: don't do this for reserved words (not quite is_reserved_word)
    // although an initial followed by a : is still invalid
    chunk_lookahead(p, &lookahead);
    if (lookahead.type == TOKEN_COLON) {
      stack_inc(p, STATE__EXPECT_COLON);
      out->type = TOKEN_LABEL;
      return 0;
    }
  }

  // look for async function modifier
  if (len == 5 && !memcmp(s, "async", 5)) {
    if (!lookahead.p) {
      // TODO: neaten up dup lookahead calls
      chunk_lookahead(p, &lookahead);
    }
    if (lookahead.type == TOKEN_LIT &&
        lookahead.len == 8
        && !memcmp(lookahead.p, "function", 8)) {
      // FIXME: mark function as 'async' so await is a keyword
      p->curr->flag |= flag;
      out->type = TOKEN_KEYWORD;
      return 0;
    } else if (vtoken_tc(lookahead, TOKEN_PAREN, '(')) {
      // FIXME: without an aggregious lookahead, we don't know whether this is-
      //   async (x, y, lots, of, stuff) => {}  // arrow function def
      // or..
      //   async (x, y, lots, of, stuff);  // function call
      return 0;
    }
  }

  // otherwise, it's a symbol
  out->type = TOKEN_SYMBOL;
  p->curr->flag = FLAG__VALUE;
  return 0;
}

#ifdef __EMSCRIPTEN__
int prsr_log(parserdef *p, token *out, int ret) {
  return ret;
}
#else
int prsr_log(parserdef *p, token *out, int ret) {
  if (ret == ERROR__UNEXPECTED) {
    printf("unexpected: [%d] tok=%d %.*s\n", out->line_no, out->len, out->type, out->p);
  }
  parserstack *tmp = p->curr;
  while (tmp != p->stack) {
    printf("... state=%d value=%d flag=%d\n", tmp->state, tmp->value, tmp->flag);
    --tmp;
  }
  printf("error: %d\n", ret);
  return ret;
}
#endif

int prsr_slash_is_op(parserdef *p) {
  for (parserstack *s = p->curr; s != p->stack; --s) {
    if (s->state == STATE__ZERO) {
      return s->flag & FLAG__VALUE;
    }
  }
  return 0;
}

int prsr_next(parserdef *p, token *out) {
  int ret;

  if (p->next.p) {
    *out = p->next;
    p->next.p = NULL;
  } else {
    int slash_is_op = prsr_slash_is_op(p);
    ret = prsr_next_token(&p->td, slash_is_op, out);
    if (ret) {
      return ret;
    }
  }

  int asi;
retry:
  asi = prsr_generates_asi(p, out);
  if (asi && out->type != TOKEN_SEMICOLON) {
    p->next = *out;
    out->len = 0;
    out->type = TOKEN_SEMICOLON;
    if (asi < 0) {
      out->line_no = p->prev.line_no;
    }
  } else if (out->type == TOKEN_COMMENT) {
    return 0;
  }

  ret = chunk_inner(p, out);
  if (ret) {
    if (ret == ERROR__RETRY) {
      goto retry;
    }
    return prsr_log(p, out, ret);
  }

  if (p->curr == p->stack || p->curr >= p->stack + __STACK_SIZE) {
    return ERROR__STACK;
  }
  if (out->type == p->prev.type && out->p == p->prev.p) {
    return ERROR__DUP;
  }
  p->prev = *out;
  return 0;
}

int prsr_parser_init(parserdef *p, char *buf) {
  bzero(p, sizeof(parserdef));
  p->td = prsr_init_token(buf);
  p->curr = p->stack + 1;
  p->curr->flag = FLAG__INITIAL;
  return 0;
}

int prsr_fp(char *buf, int (*fp)(token *)) {
  parserdef p;
  prsr_parser_init(&p, buf);

  token out;
  int ret;
  while (!(ret = prsr_next(&p, &out)) && out.type) {
    fp(&out);
  }
  return ret;
}