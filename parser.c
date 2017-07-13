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
#define STATE__RESTRICT         4  // if prev on different line, semicolon must appear
#define STATE__OPTIONAL_LABEL   5
#define STATE__EXPECT_ID        6
#define STATE__EXPECT_COLON     7
#define STATE__EXPECT_SEMICOLON 8
#define STATE__CONTROL          9
#define STATE__CONTROL_DO_WHILE 10
#define STATE__FUNCTION         11
#define STATE__CLASS            12

// flags for zero state
#define FLAG__INITIAL 1
#define FLAG__VALUE   2


#define FLAG__TYPE_STATEMENT 32
#define FLAG__TYPE_ARRAY     64
#define FLAG__TYPE_PAREN     128
#define FLAG__TYPEMASK (32 | 64 | 128)


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
  if (p->curr->state != STATE_ZERO || (p->curr->type & FLAG__TYPEMASK)) {
    return 0;  // TODO: always?
  } else if (!p->prev.line_no || p->prev.line_no == out->line_no) {
    return 0;  // not eligible, same line
  }

  if (p->prev.type == TOKEN_KEYWORD && is_asi_keyword(p->prev.s, p->prev.len)) {

    // a single-line comment forces ASI, even though it technically doesn't contain newline
    if (out->type == TOKEN_COMMENT && p[1] == '/') {
      return 1;
    }

  }

  // nb. The rest of this method can be as slow as we like, as it only exists to punish the
  // terrible people who write JavaScript without semicolons.

  switch (out->type) {
    case TOKEN_LIT:
      return !is_op_keyword(out->s, out->len) && (p->curr->flag & FLAG_VALUE);

    case TOKEN_OP:
      return (p->curr->flag & FLAG_VALUE) && is_double_addsub(out->s, out->len);

    case TOKEN_STRING:
    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
      return (flag & FLAG_VALUE);
  }

  return 0;
}

int chunk_inner(parserdef *p, token *out) {
  char *s = out->p;
  int len = out->len;

  switch (p->curr->state) {
    case STATE__ZERO:
      break;  // normal state, continue below

    case STATE__RESTRICT:
      return ERROR__INTERNAL;  // should be caught outside

    case STATE__OPTIONAL_LABEL:
      stack_dec(p);
      if (out->type == TOKEN_SEMICOLON) {
        break;  // optional, so we can skip to a semicolon
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

    case STATE__CONTROL:
      if (out->type == TOKEN_PAREN && s[0] == '(') {
        // swap for paren state, adopting previously set flags ('for' sets initial)
        p->curr->flag |= FLAG__TYPE_PAREN;
        p->curr->state = STATE__ZERO;
        return 0;
      }
      stack_dec(p);  // apparently weren't in control after all
      break;

    default:
      printf("got unhandled state: %d\n", p->curr->state);
      return ERROR__TODO;
  }

  int flag = p->curr->flag;
  int type = flag & FLAG__TYPEMASK;
  p->curr->flag = type;

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
      if (flag & FLAG__VALUE) {
        return ERROR__EXPECT_ZERO;
      }
      return 0;

    case TOKEN_PAREN:
      if (s[0] == '(') {
        return stack_inc(p, STATE__ZERO, FLAG__TYPE_PAREN);
      } else if (p->curr->state == STATE__ZERO && type == FLAG__TYPE_PAREN) {
        return stack_dec(p);
      }
      return ERROR__UNEXPECTED;

    case TOKEN_ARRAY:
      if (s[0] == '[') {
        return stack_inc(p, STATE__ZERO, FLAG__TYPE_ARRAY);
      } else if (p->curr->state == STATE__ZERO && type == FLAG__TYPE_ARRAY) {
        return stack_dec(p);
      }
      return ERROR__UNEXPECTED;

    // TODO: TOKEN_BRACE

    case TOKEN_DOT:
      if (!(flag & FLAG__VALUE)) {
        // unexpected
      }
      return stack_inc(p, STATE__EXPECT_ID, 0);

    case TOKEN_LIT:
      break;  // continues excitement below

    default:
      printf("got unhandled token in chunk_inner: %d\n", out->type);
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
        return ERROR__EXPECT_ZERO;  // let[ is ambiguous, leave as TOKEN_LIT
      }
    } else if (s[0] == 'r' || s[0] == 't') {
      // 'return' and 'throw' are restricted
      stack_inc(p, STATE__RESTRICT, 0);
    }

    out->type = TOKEN_KEYWORD;
    return ERROR__EXPECT_ZERO;
  } while(0);

  // found keyword which is an expr: e.g., 'await foo'
  if (is_expr_keyword(s, len)) {
    out->type = TOKEN_KEYWORD;
    if (s[0] == 'y') {
      // 'yield' is restricted
      stack_inc(p, STATE__RESTRICT, 0);
    }
    return 0;
  }

  if (is_hoist_keyword(s, len)) {
    int state = (s[0] == 'f' ? STATE__FUNCTION : STATE__CLASS);
    stack_inc(p, state, 0);

    if ((flag & FLAG__VALUE) || (!type && (flag & FLAG__INITIAL))) {
      // FIXME: this is sometimes a decl, sometimes a statement
      return ERROR__EXPECT_ZERO;
    }
    // we're definitely a statement
    return ERROR__TODO;
  }

  // from here down, if we see a 

  do {
    int asi = 0;
    parserstack *was = p->curr;
    if (is_control_keyword(s, len)) {
      int state = (s[0] == 'd' && len == 2) ? STATE__CONTROL_DO_WHILE : STATE__CONTROL;
      int flag = (s[0] == 'f' && len == 3) ? FLAG__INITIAL : 0;
      stack_inc(p, state, flag);
    } else if (is_label_keyword(s, len)) {
      // look for next optional label ('break foo;')
      asi = 1;
      stack_inc(p, STATE__OPTIONAL_LABEL, 0);
    } else if (is_isolated_keyword(s, len)) {
      // matches 'debugger'
      asi = 1;
    } else if (is_labellike_keyword(s, len)) {
      if (s[0] == 'd') {
        stack_inc(p, STATE__EXPECT_COLON, 0);
      } else {
        // TODO: match statement inside case?
      }
    } else {
      break;
    }

    if (asi) {
      // 'break', 'continue' and 'debugger' are restricted
      stack_inc(p, STATE__RESTRICT, 0);
    }

    was->flag |= FLAG__INITIAL;  // reset whatever it was to be initial
    out->type = TOKEN_KEYWORD;
    return ERROR__EXPECT_ZERO;
  } while (0);

  // otherwise, it's a symbol
  out->type = TOKEN_SYMBOL;
  p->curr->flag |= FLAG__VALUE;
  if (flag & FLAG__VALUE) {
    return ERROR__EXPECT_ZERO;
  }
  return 0;
}

int prsr_log(parserdef *p, token *out, int ret) {
  if (ret == ERROR__UNEXPECTED) {
    printf("unexpected: [%d] tok=%d %.*s\n", out->line_no, out->len, out->type, out->p);
  }
  parserstack *tmp = p->curr;
  while (tmp != p->stack) {
    printf("... state=%d flag=%d\n", tmp->state, tmp->flag);
    --tmp;
  }
  printf("error: %d\n", ret);
  return ret;
}

int prsr_next(parserdef *p, token *out) {
  if (p->next.p) {
    *out = p->next;
    p->next.p = NULL;
  }

  int slash_is_op = (p->curr->state == STATE__ZERO) && (p->curr->flag & FLAG__VALUE);
  int ret = prsr_next_token(&p->td, slash_is_op, out);
  if (ret) {
    return ret;
  }

  if (prsr_generates_asi(p, out)) {
    p->next = *out;
    out->len = 0;
    out->type = TOKEN_SEMICOLON;
  }

  ret = chunk_inner(p, out);
  if (ret) {
    return ret;
  }

  if (p->curr == p->stack || p->curr >= p->stack + __STACK_SIZE) {
    return ERROR__STACK;
  }
  p->prev = *out;
  return 0;
}


int prsr_next(parserdef *p, token *out) {
  // if an ASI was sent, return the real next statement and clear
  if (p->after_asi.p) {
    *out = p->after_asi;
    p->prev = p->after_asi;
    p->after_asi.p = NULL;
    return 0;
  }

  // get token, either post-restrict or anew
  int ret;
  if (p->after_restrict_semicolon.p) {
    *out = p->after_restrict_semicolon;
    p->after_restrict_semicolon.p = NULL;
  } else {
    // this check won't look under e.g. STATE__RESTRICT, but _only_ the zero state has 'value'
    int slash_is_op = (p->curr->state == STATE__ZERO) && (p->curr->flag & FLAG__VALUE);
    ret = prsr_next_token(&p->td, slash_is_op, out);
    if (ret || out->type == TOKEN_COMMENT) {
      return ret; 
    }
  }

  // insert fake semicolon for restricted keyword
  if (p->curr->state == STATE__RESTRICT) {
    int err = stack_dec(p);
    if (err) {
      return err;
    }
    if (p->prev.line_no != out->line_no || out->type != TOKEN_SEMICOLON) {
      p->after_restrict_semicolon = *out;
      out->len = 0;
      out->type = TOKEN_SEMICOLON;
    }
  }

  // actual chunk call
  ret = chunk_inner(p, out);
  if (ret) {
    if (ret != ERROR__EXPECT_ZERO) {
      return prsr_log(p, out, ret);
    }

    if (!p->prev.line_no) {
      // not an ASI, first statement
    } else if (p->prev.line_no != out->line_no) {
      p->after_asi = *out;
      out->len = 0;
      out->type = TOKEN_SEMICOLON;
    } else {
      // this is a syntax error, but we don't report it
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