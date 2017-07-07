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

#define STACK__BRACE          0   // is {}s
#define STACK__ARRAY          1   // is []s
#define STACK__PAREN          2   // is ()s
#define STACK__OBJECT         4   // we are an object literal-ish
#define STACK__TYPEMASK       6   // mask for types

#define STACK__CONTROL        32  // this is a control stack

#define STACK__EXPR           8   // is expr- no var/const/throw, and hoisted are statements
#define STACK__VALUE          16  // currently have 'value' (thus / is op, not regexp)
// TODO: can't have VALUE without EXPR

#define FLAG__EXPECT_ID       1
#define FLAG__EXPECT_LABEL    2
#define FLAG__RESTRICT        4

int chunk_inner(parserdef *p, token *out) {
  int s = p->stack[p->depth];
  int ret = prsr_next_token(&p->td, s & STACK__VALUE, out);
  if (ret) {
    return ret; 
  }

  int flags = p->flags;
  p->flags = 0;

  // we're a colon following a label, no state changes
  if (out->type == TOKEN_COLON && p->prev_type == TOKEN_LABEL) {
    return 0;
  }

  // simple cases
  switch (out->type) {
    case TOKEN_NEWLINE:
      if (flags & FLAG__RESTRICT) {
        p->emit_asi = 1;
      }
      return 0;

    case TOKEN_SEMICOLON:
      p->stack[p->depth] &= ~(STACK__VALUE | STACK__EXPR);
      return 0;

    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
    case TOKEN_STRING:
      p->stack[p->depth] |= STACK__VALUE;
      // fall-through

    case TOKEN_COMMA:
    case TOKEN_TERNARY:
    case TOKEN_COLON:
      p->stack[p->depth] |= STACK__EXPR;
      return 0;

    case TOKEN_OP:
      // FIXME: not always: postfix ++'s have value
      p->stack[p->depth] &= ~STACK__VALUE;
      return 0;

    case TOKEN_LIT:
      break;

    default:
      return 0;
  }

  // TODO: check for LHS of object literal

  out->type = TOKEN_SYMBOL;  // assume symbol
  do {

    // short-circuit for IDs (e.g. 'class Foo', 'foo.bar')
    if (flags & FLAG__EXPECT_ID) {
      return 0;  // explicitly return
    }

    // we just had a continue or break, must be a label
    if (flags & FLAG__EXPECT_LABEL) {
      out->type = TOKEN_LABEL;
      break;
    }

    // look for in/instanceof, which are always ops
    if (is_op_keyword(out->p, out->len)) {
      out->type = TOKEN_OP;
      p->stack[p->depth] &= ~STACK__VALUE;
      break;
    }

    // look for await etc, which act a bit like ops (e.g., 'var x = await foo')
    if (is_expr_keyword(out->p, out->len)) {
      out->type = TOKEN_KEYWORD;
      p->stack[p->depth] &= ~STACK__VALUE;
      break;
    }

    // hoist or looks like a hoist
    if (is_hoist_keyword(out->p, out->len)) {
      out->type = TOKEN_KEYWORD;
      p->flags |= FLAG__EXPECT_ID;

      // TODO: mark next as object literal if 'class'

      if (s & STACK__EXPR) {
        p->stack[p->depth] |= STACK__VALUE;
      }

      break;
    }

    // set restrict on ASI
    if (is_asi_keyword(out->p, out->len)) {
      out->type = TOKEN_KEYWORD;
      p->flags |= FLAG__RESTRICT;
    }

    if (s & STACK__EXPR) {
      if (out->type != TOKEN_KEYWORD && is_keyword(out->p, out->len)) {
        // no other keywords are valid here, but hey /shrug
        out->type = TOKEN_KEYWORD;
      }
    } else {
      // this may possibly be a label, look for colon
      // look for backticks
      // TODO

      out->type = TOKEN_KEYWORD;

      if (is_control_keyword(out->p, out->len)) {
        // got an if/for/while etc, next ()'s are control
        p->stack[p->depth+1] |= STACK__CONTROL;
      } else if (is_label_keyword(out->p, out->len)) {
        // break or continue, expects a label to follow
        p->flags |= FLAG__EXPECT_LABEL;
      } else if (is_begin_expr_keyword(out->p, out->len)) {
        // var/let/const must have a following ID, can't have new/await etc
        p->stack[p->depth] |= STACK__EXPR;
        p->flags |= FLAG__EXPECT_ID;
      } else if (is_keyword(out->p, out->len)) {
        // not an expression, but is a keyword
      } else {
        out->type = TOKEN_SYMBOL;
      }
    }

  } while (0);

  if (out->type == TOKEN_SYMBOL) {
    p->stack[p->depth] |= (STACK__VALUE | STACK__EXPR);
  }

  return 0;
}

int prsr_next(parserdef *p, token *out) {
  // if an ASI was pending, return it and clear
  if (p->pending_asi.p) {
    *out = p->pending_asi;
    p->pending_asi.p = NULL;
    return 0;
  }

  // actual chunk call
  int ret = chunk_inner(p, out);
  if (ret) {
    return ret;
  }

  // requested that we emit an ASI before this token, so store and return an ASI
  if (p->emit_asi) {
    p->emit_asi = 0;
    p->pending_asi = *out;
    out->len = 0;
    out->type = TOKEN_SEMICOLON;
    p->prev_type = TOKEN_SEMICOLON;  // for next chunk_inner call
    return 0;
  }
  p->prev_type = out->type;

  int s = p->stack[p->depth];
  if ((s & STACK__VALUE) && !(s & STACK__EXPR)) {
    return ERROR__VALUE_NO_EXPR;
  }

  return 0;
}

int prsr_fp(char *buf, int (*fp)(token *)) {
  parserdef p;
  bzero(&p, sizeof(p));
  p.td = prsr_init(buf);
  p.depth = 1;

  token out;
  int ret;
  while (!(ret = prsr_next(&p, &out)) && out.type) {
    fp(&out);
  }
  return ret;
}
