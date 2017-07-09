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
#define STACK__TYPEMASK       3   // mask for types

#define STACK__OBJECT         4   // is object literal
#define STACK__CONTROL        8   // is a control stack

#define STACK__EXPR           16  // is expr- no var/const/throw, and hoisted are statements
#define STACK__VALUE          32  // currently have 'value' (thus / is op, not regexp)
// TODO: can't have VALUE without EXPR

#define FLAG__EXPECT_ID       1
#define FLAG__EXPECT_LABEL    2
#define FLAG__RESTRICT        4

#define FLAG__INVALID_PROD    64   // invalid grammar production, emit ASI if possible
#define FLAG__ERROR_STACK     128  // invalid stack state

int modify_stack(parserdef *p, int inc, int type) {
  if (inc) {
    if (++p->depth == _TOKEN_STACK_SIZE) {
      p->flags |= FLAG__ERROR_STACK;
    }
    uint8_t s = p->stack[p->depth];  // retain values set here
    p->stack[p->depth] = (s & ~STACK__TYPEMASK) | (type & STACK__TYPEMASK);
  } else {
    uint8_t prev = p->stack[p->depth];
    if ((prev & STACK__TYPEMASK) != type || p->depth == 1) {
      p->flags |= FLAG__ERROR_STACK;
    }
    p->stack[p->depth+1] = 0;  // in case we set STACK__CONTROL etc but never used it
    p->stack[p->depth] = 0;
    --p->depth;
  }
  return p->flags & FLAG__ERROR_STACK;
}

#define stack_has(p, op) (p->stack[p->depth] & (op))
#define stack_next_has(p, op) (p->stack[p->depth+1] & (op))
#define stack_set(p, op) (p->stack[p->depth] |= (op))
#define stack_next_set(p, op) (p->stack[p->depth+1] |= (op))
#define stack_clear(p, op) (p->stack[p->depth] &= ~(op))

int chunk_inner(parserdef *p, token *out) {
  int ret = prsr_next_token(&p->td, stack_has(p, STACK__VALUE), out);
  if (ret) {
    return ret; 
  }

  // tokens ignored for parsing
  switch (out->type) {
    case TOKEN_NEWLINE:
      if (p->flags & FLAG__RESTRICT) {
        p->flags = FLAG__INVALID_PROD;
      }
      // fall-through

    case TOKEN_COMMENT:
      return 0;
  }

  int flags = p->flags;
  p->flags = 0;

  // we're a colon following a label, no state changes
  if (out->type == TOKEN_COLON && p->prev_type == TOKEN_LABEL) {
    return 0;
  }

  // brackets
  switch (out->type) {
    case TOKEN_PAREN:
      if (out->p[0] == '(') {
        // left-hand side: look for whether this will be a value when done
        int next_control = stack_next_has(p, STACK__CONTROL);
        if (!next_control && !stack_has(p, STACK__EXPR)) {
          // this will probably result in a value
          stack_set(p, STACK__EXPR | STACK__VALUE);
        }

        // inside a control, this is effectively an expr (some exceptions... for (var...))
        modify_stack(p, 1, STACK__PAREN);
        stack_set(p, STACK__EXPR);
      } else {
        // right-hand: just close
        modify_stack(p, 0, STACK__PAREN);
      }
      return 0;

    case TOKEN_ARRAY:
      if (out->p[0] == '[') {
        // left-hand: open and set expr
        modify_stack(p, 1, STACK__ARRAY);
        stack_set(p, STACK__EXPR);
      } else {
        modify_stack(p, 0, STACK__ARRAY);
      }
      return 0;

    case TOKEN_BRACE:
      if (out->p[0] == '{') {
        // if we're in an open expr, this is an object
        int is_value = stack_has(p, STACK__VALUE);

        if (p->prev_type != TOKEN_ARROW && stack_has(p, STACK__EXPR) && !stack_has(p, STACK__VALUE)) {
          stack_next_set(p, STACK__OBJECT);
        }
        modify_stack(p, 1, STACK__BRACE);
        // FIXME: functions probably seen as object..
        printf("open brace, object=%d value=%d\n", stack_has(p, STACK__OBJECT), is_value);
      } else {
        // emit ASI if this ended with an open expr
        if (!stack_has(p, STACK__OBJECT) && stack_has(p, STACK__EXPR)) {
          p->flags |= FLAG__INVALID_PROD;
        }
        modify_stack(p, 0, STACK__BRACE);
      }

      return 0;
  }

  // simple cases
  switch (out->type) {
    case TOKEN_NEWLINE:
    case TOKEN_SEMICOLON:
      // some cases handled by caller
      return 0;

    case TOKEN_DOT:
      p->flags |= FLAG__EXPECT_ID;
      return 0;

    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
    case TOKEN_STRING:
      stack_set(p, STACK__VALUE);
      // fall-through

    case TOKEN_COMMA:
    case TOKEN_TERNARY:
    case TOKEN_COLON:
      stack_set(p, STACK__EXPR);
      return 0;

    case TOKEN_OP:
      if (stack_has(p, STACK__VALUE) && is_double_addsub(out->p, out->len)) {
        if (p->prev_type != TOKEN_NEWLINE) {
          return 0;  // only a postfix if there was a value, and prev wasn't newline
        }
        p->flags |= FLAG__INVALID_PROD;
      }
      stack_clear(p, STACK__VALUE);
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
      stack_clear(p, STACK__VALUE);
      break;
    }

    // look for await etc, which act a bit like ops (e.g., 'var x = await foo')
    if (is_expr_keyword(out->p, out->len)) {
      out->type = TOKEN_KEYWORD;
      stack_clear(p, STACK__VALUE);
      break;
    }

    // hoist or looks like a hoist
    if (is_hoist_keyword(out->p, out->len)) {
      out->type = TOKEN_KEYWORD;
      p->flags |= FLAG__EXPECT_ID;

      if (out->len == 5) {
        // this class {} is an object literal
        stack_next_set(p, STACK__OBJECT);
      }

      if (stack_has(p, STACK__EXPR)) {
        // next {} builds a value
        stack_set(p, STACK__VALUE);
      }
      printf("got hoist, is expr=%d value=%d\n", stack_has(p, STACK__EXPR), stack_has(p, STACK__VALUE));

      break;
    }

    // set restrict on ASI
    if (is_asi_keyword(out->p, out->len)) {
      out->type = TOKEN_KEYWORD;
      p->flags |= FLAG__RESTRICT;
    }

    if (stack_has(p, STACK__EXPR)) {
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
        stack_next_set(p, STACK__CONTROL);
      } else if (is_label_keyword(out->p, out->len)) {
        // break or continue, expects a label to follow
        p->flags |= FLAG__EXPECT_LABEL;
      } else if (is_begin_expr_keyword(out->p, out->len)) {
        // should be followed by an ID
        stack_set(p, STACK__EXPR);
      } else if (is_keyword(out->p, out->len)) {
        // not an expression, but is a keyword
      } else {
        out->type = TOKEN_SYMBOL;
      }
    }

  } while (0);

  if (out->type == TOKEN_SYMBOL) {
    stack_set(p, STACK__VALUE | STACK__EXPR);
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

  if ((p->flags & FLAG__INVALID_PROD) && !stack_has(p, STACK__TYPEMASK)) {
    // only ASI in normal {} control
  } else if ((p->flags & FLAG__INVALID_PROD) || out->type == TOKEN_SEMICOLON) {
    // clear state if we see a semicolon
    stack_clear(p, STACK__VALUE | STACK__EXPR);
    if (stack_has(p, STACK__CONTROL)) {
      stack_set(p, STACK__EXPR);  // after ; in control (e.g., for) can't have 'var'
    }

    // requested that we emit an ASI before this token, so store and return an ASI
    if ((p->flags & FLAG__INVALID_PROD)) {
      p->flags &= ~FLAG__INVALID_PROD;
      p->pending_asi = *out;
      out->len = 0;
      out->type = TOKEN_SEMICOLON;
    }
  }
  p->prev_type = out->type;

  // error cases
  if (p->flags & FLAG__INVALID_PROD) {
    return ERROR__SYNTAX;
  } else if (p->flags & FLAG__ERROR_STACK) {
    return ERROR__STACK;  // stack too big or small, invalid value
  } else if (stack_has(p, STACK__VALUE) && !stack_has(p, STACK__EXPR)) {
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
