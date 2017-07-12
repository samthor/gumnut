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


#define STATE__ZERO             0
#define STATE__OPTIONAL_LABEL   5
#define STATE__EXPECT_ID        6
#define STATE__EXPECT_COLON     7
#define STATE__EXPECT_SEMICOLON 8
#define STATE__CONTROL_FOR      9
#define STATE__CONTROL_DO_WHILE 10

#define FLAG__INITIAL 1
#define FLAG__VALUE   2


int stack_inc(parserdef *p, uint8_t state, uint8_t flag) {
  ++p->curr;
  p->curr->state = state;
  p->curr->flag = flag;
  if (p->curr == p->stack + __STACK_SIZE - 1) {
    return ERROR__STACK;
  }
  return 0;
}

int stack_dec(parserdef *p) {
  --p->curr;
  if (p->curr == p->stack) {
    return ERROR__STACK;
  }
  return 0;
}

int chunk_inner(parserdef *p, token *out) {
  char *s = out->p;
  int len = out->len;

  switch (p->curr->state) {
    case STATE__ZERO:
      break;

    case STATE__OPTIONAL_LABEL:
      stack_dec(p);
      if (out->type == TOKEN_SEMICOLON) {
        break;
      } else if (out->type != TOKEN_LIT) {
        break;  // TODO: indicate error, needs reset to zero state
      }
      out->type = TOKEN_LABEL;
      stack_inc(p, STATE__EXPECT_SEMICOLON, 0);
      return 0;

    case STATE__EXPECT_ID:
      stack_dec(p);
      if (out->type != TOKEN_LIT) {
        break;  // TODO: indicate error, maybe should reset to zero state
      }
      out->type = TOKEN_SYMBOL;
      return 0;

    case STATE__EXPECT_COLON:
      stack_dec(p);
      if (out->type != TOKEN_COLON) {
        break;  // TODO: indicate error
      }
      return 0;

    case STATE__EXPECT_SEMICOLON:
      stack_dec(p);
      if (out->type != TOKEN_SEMICOLON) {
        printf("expected semi, got: %d\n", out->type);
        break;  // TODO: indicate error, needs reset to zero state
      }
      return 0;

    case STATE__CONTROL_FOR:
      if (out->type == TOKEN_PAREN && s[0] == '(') {
        // special-case ( seen after for block, allow initial var/let/etc
        return stack_inc(p, STATE__ZERO, FLAG__INITIAL);
      }
      break;

    default:
      printf("got unhandled state: %d\n", p->curr->state);
      return ERROR__TODO;
  }

  int flag = p->curr->flag;
  p->curr->flag = 0;

  if (out->type == TOKEN_LIT && is_op_keyword(s, len)) {
    // replace 'in' and 'instanceof' with op
    out->type = TOKEN_OP;
  } else if (out->type == TOKEN_OP && (flag & FLAG__VALUE) && p->prev.line_no != out->line_no &&
        is_double_addsub(s, len)) {
    // if value, had a newline, and is a ++/-- op: force an ASI
    return ERROR__EXPECT_ZERO;
  }

  switch (out->type) {
    case TOKEN_EOF:
      if (p->curr > p->stack + 1) {
        return ERROR__UNEXPECTED;
      }
      return 0;

    case TOKEN_COMMA:
      // TODO: if _statement_, dec and return
      // fall-through

    case TOKEN_TERNARY:
    case TOKEN_COLON:
    case TOKEN_OP:
      return 0;

    case TOKEN_SEMICOLON:
      // TODO: reset to zero state
      p->curr->flag = FLAG__INITIAL;
      return 0;

    case TOKEN_STRING:
    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
      p->curr->flag |= FLAG__VALUE;
      return 0;

    case TOKEN_PAREN:
      if (s[0] == '(') {
        // FIXME: record PAREN
        return stack_inc(p, STATE__ZERO, 0);
      } else if (p->curr->state == STATE__ZERO) {
        return stack_dec(p);
      }
      return ERROR__UNEXPECTED;

    case TOKEN_ARRAY:
      if (s[0] == '[') {
        // FIXME: record ARRAY
        return stack_inc(p, STATE__ZERO, 0);
      } else if (p->curr->state == STATE__ZERO) {
        return stack_dec(p);
      }
      return ERROR__UNEXPECTED;

    // TODO: TOKEN_BRACE

    case TOKEN_DOT:
      return stack_inc(p, STATE__EXPECT_ID, 0);

    case TOKEN_LIT:
      break;  // continues excitement below

    default:
      printf("got unhandled token in chunk_inner: %d\n", out->type);
      return ERROR__TODO;
  }


  if (out->type != TOKEN_LIT) {
    return 0;
  }

  if (is_begin_expr_keyword(s, len)) {
    /*
      if (.. is "let" ..) {
        // allow at start and after
        // don't allow if a [ immediately follows it at start (ambiguous keyword/symbol)
      }
    */

    out->type = TOKEN_KEYWORD;
    return ERROR__EXPECT_ZERO;
  }


  int expect_label = 0;
  if (is_control_keyword(s, len)) {
    if (s[0] == 'f' && len == 3) {
      stack_inc(p, STATE__CONTROL_FOR, 0);
    } else if (s[0] == 'd' && len == 2) {
      stack_inc(p, STATE__CONTROL_DO_WHILE, 0);
    }
    return ERROR__EXPECT_ZERO;
  } else if (is_label_keyword(s, len)) {
    stack_inc(p, STATE__OPTIONAL_LABEL, 0);
    return ERROR__EXPECT_ZERO;
  } else if (is_isolated_keyword(s, len)) {
    return ERROR__EXPECT_ZERO;
  } else if (is_hoist_keyword(s, len)) {
    // run hoist program
  }

  if (is_labellike_keyword(s, len)) {
    out->type = TOKEN_KEYWORD;
    if (s[0] == 'd') {
      stack_inc(p, STATE__EXPECT_COLON, 0);
    }
    return ERROR__EXPECT_ZERO;
  }

  if (is_asi_keyword(s, len)) {
    // look for next non-comment token, if on next line, generate ASI
  }

  out->type = TOKEN_SYMBOL;
  return 0;
}

int prsr_next(parserdef *p, token *out) {
  // if an ASI was sent, return the real next statement and clear
  if (p->after_asi.p) {
    *out = p->after_asi;
    p->after_asi.p = NULL;
    return 0;
  }

  // get token
  int slash_is_op = (p->curr->state == STATE__ZERO) && (p->curr->flag & FLAG__VALUE);
  int ret = prsr_next_token(&p->td, slash_is_op, out);
  if (ret || out->type == TOKEN_COMMENT) {
    return ret; 
  }

  // actual chunk call
  ret = chunk_inner(p, out);
  if (ret) {
    if (ret != ERROR__EXPECT_ZERO) {
      // TODO: remove later
      if (ret == ERROR__UNEXPECTED) {
        printf("unexpected: [%d] %.*s\n", out->line_no, out->len, out->p);
      }
      parserstack *tmp = p->curr;
      while (tmp != p->stack) {
        printf("... state=%d flag=%d\n", tmp->state, tmp->flag);
        --tmp;
      }
      printf("error: %d\n", ret);

      return ret;
    }

    if (!p->prev.line_no) {
      // not an ASI, first statement
    } else if (p->prev.line_no == out->line_no) {
      // this is a syntax error, but we don't report it
    } else {
      p->after_asi = *out;
      out->len = 0;
      out->type = TOKEN_SEMICOLON;
    }
  }
  p->prev = *out;

  if (p->curr == p->stack || p->curr >= p->stack + __STACK_SIZE) {
    return ERROR__STACK;
  }
  return 0;
}

int prsr_parser_init(parserdef *p, char *buf) {
  bzero(p, sizeof(parserdef));
  p->td = prsr_init_token(buf);
  p->stack[0].state = STATE__ZERO;
  p->curr = p->stack + 1; 
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