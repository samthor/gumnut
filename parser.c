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

#define STATE__ZERO          -1  // zero stack state
#define STATE__BLOCK          0  // execution block
#define STATE__STATEMENT      1  // single statement only
#define STATE__PARENS         2  // (...) block: on its own, call or def
#define STATE__ARRAY          3  // array or index into array
#define STATE__CONTROL        4  // generic control statement: xx (foo) {}
#define STATE__OBJECT         5
#define STATE__EXPECT_ID      6
#define STATE__EXPECT_LABEL   7
#define STATE__ARROW          8

// flags for parser
#define PARSER__RESTRICT        1  // causes ASI on newline

// flags for default states
#define FLAG__VALUE           1  // nb. don't re-use
#define FLAG__INITIAL         2  // allows 'var' etc, as well as function/class statements

// flags for STATE__CONTROL
#define FLAG__DO_WHILE        4
#define FLAG__SEEN_PAREN      8


int stack_inc(parserdef *p, int state, int flag) {
  ++p->curr;
  p->curr->state = state;
  p->curr->flag = flag;
  if (p->curr - p->stack == __STACK_SIZE - 1) {
    return -1;
  }
  return 0;
}

int stack_dec(parserdef *p) {
  --p->curr;
  if (p->curr == p->stack) {
    return -1;
  }
  return 0;
}

int chunk_normal_semicolon(parserdef *p, token *out) {
  if (p->curr->state == STATE__BLOCK) {
    p->curr->flag = FLAG__INITIAL;
    if (!out) {
      return ERROR__SYNTAX_ASI;  // this is an ASI, be helpful to caller
    }
  }
  return 0;
}

int chunk_inner(parserdef *p, token *out) {
  parserstack *curr = p->curr;
  char *s = out->p;
  int len = out->len;

  switch (curr->state) {
    case STATE__BLOCK:
    case STATE__PARENS:
    case STATE__ARRAY:
      break;  // secret tip about writing JS parsers: these are all just expression lists

    // TODO: implement complex states here:
    // * function/class
    // * object literal
    // * control blocks (+do/while)

    case STATE__CONTROL:
      if (!(curr->flag & FLAG__SEEN_PAREN) && out->type == TOKEN_PAREN && s[0] == '(') {
        curr->flag |= FLAG__SEEN_PAREN;
        stack_inc(p, STATE__PARENS, FLAG__INITIAL);
        return 0;
      }

      // FIXME: This doesn't work for do/while, which allows "do console.info; while (0);"
      // FIXME: we need a notion of "single statement" (returns on newline)
      if (out->type == TOKEN_BRACE && s[0] == '{') {
        curr->state = STATE__BLOCK;  // swap state
        curr->flag = FLAG__INITIAL;
        return 0;
      }
      stack_dec(p);
      break;

    case STATE__EXPECT_ID:
      stack_dec(p);
      if (out->type == TOKEN_LIT) {
        out->type = TOKEN_SYMBOL;
        return 0;
      }
      break;

    case STATE__EXPECT_LABEL:
      stack_dec(p);
      if (out->type == TOKEN_LIT) {
        out->type = TOKEN_LABEL;
        return 0;
      }
      break;

    case STATE__ARROW:
      if (out->type == TOKEN_BRACE && s[0] == '{') {
        curr->state = STATE__BLOCK;  // swap state
        curr->flag = FLAG__INITIAL;
        return 0;
      } 
      // FIXME: swap out for 'statement'
      stack_dec(p);
      break;

    default:
      printf("got unhandled state: %d\n", curr->state);
      return ERROR__TODO;
  }

  // zero states
  switch (out->type) {
    case TOKEN_EOF:
      if (curr->state != STATE__BLOCK || curr != (p->stack + 1)) {
        return ERROR__STACK;
      } else if (!(curr->flag & FLAG__INITIAL)) {
        return chunk_normal_semicolon(p, NULL);;
      }
      return 0;
    case TOKEN_NEWLINE:
      if (p->flag & PARSER__RESTRICT) {
        p->flag = 0;
        return chunk_normal_semicolon(p, NULL);
      }
      // fall-through
    case TOKEN_COMMENT:
      return 0;
  }

  // replace 'in' and 'instanceof' with op
  if (out->type == TOKEN_LIT && is_op_keyword(s, len)) {
    out->type = TOKEN_OP;
  } else if (out->type == TOKEN_OP && is_double_addsub(s, len)) {
    // ++/--: if flag is a value; op in a block and after a newline; maybe yield an ASI
    if ((curr->flag & FLAG__VALUE) && (p->prev_type == TOKEN_NEWLINE)) {
      return chunk_normal_semicolon(p, NULL);
    }
    return 0;  // the addsub doesn't change flags anyway
  }

  int flag = curr->flag;
  curr->flag = 0;

  // non-literal tokens
  switch (out->type) {
    case TOKEN_TERNARY:
    case TOKEN_COLON:
    case TOKEN_COMMA:
    case TOKEN_OP:
      return 0;

    case TOKEN_SEMICOLON:
      return chunk_normal_semicolon(p, out);

    case TOKEN_STRING:
    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
      curr->flag = FLAG__VALUE;
      return 0;

    case TOKEN_ARROW:
      curr->flag = FLAG__VALUE;
      return stack_inc(p, STATE__ARROW, 0);

    case TOKEN_PAREN:
      if (s[0] == '(') {
        return stack_inc(p, STATE__PARENS, 0);
      } else if (curr->state == STATE__PARENS) {
        return stack_dec(p);
      }
      return ERROR__UNEXPECTED;

    case TOKEN_ARRAY:
      if (s[0] == '[') {
        return stack_inc(p, STATE__ARRAY, 0);
      } else if (curr->state == STATE__ARRAY) {
        return stack_dec(p);
      }
      return ERROR__UNEXPECTED;

    case TOKEN_BRACE:
      if (s[0] == '{') {
        // this is a block if we're the first expr (orphaned block)
        if (flag & FLAG__INITIAL) {
          curr->flag |= FLAG__INITIAL;  // restore to intial after this
          return stack_inc(p, STATE__BLOCK, FLAG__INITIAL);
        }
        return stack_inc(p, STATE__OBJECT, 0);
      } else if (curr->state == STATE__BLOCK && curr > p->stack) {   // only look for BLOCK here
        int err = 0;
        if (!(flag & FLAG__INITIAL)) {
          err = chunk_normal_semicolon(p, NULL);  // ASI if something is pending
        }
        stack_dec(p);
        return err;
      }
      return ERROR__UNEXPECTED;

    case TOKEN_DOT:
      return stack_inc(p, STATE__EXPECT_ID, 0);

    case TOKEN_LIT:
      break;  // continues excitement below

    default:
      printf("got unhandled token in chunk_inner: %d\n", out->type);
      return ERROR__TODO;
  }

  // look for initial tokens
  if ((flag & FLAG__INITIAL) && is_begin_expr_keyword(s, len)) {
    out->type = TOKEN_KEYWORD;
    return 0;
  }
  // FIXME: for some reason everything but "let" is still a keyword even in invalid places.

  // next token should also be part of expr
  if (is_expr_keyword(s, len)) {
    out->type = TOKEN_KEYWORD;
    return 0;
  }

  // TODO: look for 'async' and lookahead +1 for 'function'
  // TODO: lookahead for :'s
  // TODO: look for case, default

  int restrict_keyword = 0;

  // found a function or class
  if (is_control_keyword(s, len)) {
    int flag = 0;
    if (s[0] == 'd') {
      flag = FLAG__DO_WHILE;
    }
    return stack_inc(p, STATE__CONTROL, flag);
  } else if (is_hoist_keyword(s, len)) {
    // TODO: statement or expr based on STATE__ZERO/STATE__EXOR
    return ERROR__TODO;
  } else if (is_label_keyword(s, len)) {
    p->flag |= PARSER__RESTRICT;
    return stack_inc(p, STATE__EXPECT_LABEL, 0);
  }

  curr->flag |= FLAG__VALUE;
  out->type = TOKEN_SYMBOL;
  return 0;
}

int prsr_next(parserdef *p, token *out) {
  // if an ASI was pending, return it and clear
  if (p->pending_asi.p) {
    *out = p->pending_asi;
    p->pending_asi.p = NULL;
    return 0;
  }

  // get token
  parserstack *curr = p->curr;
  int ret = prsr_next_token(&p->td, curr->flag & FLAG__VALUE, out);
  if (ret) {
    return ret; 
  }

  // actual chunk call
  ret = chunk_inner(p, out);
  if (ret) {
    if (ret != ERROR__SYNTAX_ASI) {
      return ret;
    }
    p->pending_asi = *out;
    out->len = 0;
    out->type = TOKEN_SEMICOLON;
  }
  p->prev_type = out->type;

  if (p->curr == p->stack) {
    return ERROR__STACK;
  }
  return 0;
}

int prsr_fp(char *buf, int (*fp)(token *)) {
  parserdef p;
  bzero(&p, sizeof(p));
  p.td = prsr_init(buf);
  p.stack[0].state = STATE__ZERO;
  p.curr = p.stack + 1; 
  p.curr->flag = FLAG__INITIAL;

  token out;
  int ret;
  while (!(ret = prsr_next(&p, &out)) && out.type) {
    fp(&out);
  }
  return ret;
}
