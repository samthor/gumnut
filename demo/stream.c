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
#include "stream.h"
#include "../utils.h"

static int token_string(token *t, char *s, int len) {
  return t->len == len && !memcmp(t->p, s, len);
}

static int type_implies_statement(int type) {
  switch (type) {
    case TOKEN_SEMICOLON:
    case TOKEN_COMMA:
    case TOKEN_SPREAD:
    case TOKEN_DOT:
    case TOKEN_OP:
    case TOKEN_ARROW:  // `=> /foo/`
    case TOKEN_COLON:  // `foo: /foo/`, or `x ? y : /foo/`
    case TOKEN_TERNARY:
      return 1;
  }
  return 0;
}

static int token_is_dict(streamlev *lev) {
  // called when the parser sees TOKEN_BRACE

  switch (lev->prev1.type) {
    case TOKEN_BRACE:
      // FIXME: this is `{} {}`, which _probably_ is always a block
      return 0;

    case TOKEN_COLON:
      // e.g. `{foo:{`
      return lev->is_dict;

    case TOKEN_KEYWORD:
      // e.g. `case {`, `return {`, `var/let/const {`
      return is_case(lev->prev1.p, lev->prev1.len) ||
          is_begin_expr_keyword(lev->prev1.p, lev->prev1.len);

      // nb. `return` and `yield` etc are ASI'ed elsewhere, ignore line no's

    case TOKEN_COMMA:  // for ARRAY, PAREN etc
    case TOKEN_OP:     // includes 'instanceof', 'yield' etc
    case TOKEN_TERNARY:
      return 1;
  }
  return 0;
}

static int stack_mod(streamdef *sd, token *t) {
  switch (t->type) {
    case TOKEN_BRACE:
    case TOKEN_ARRAY:
    case TOKEN_PAREN:
    case TOKEN_T_BRACE:
      break;

    default:
      return 0;
  }

  if (sd->dlev == __STACK_SIZE - 1) {
    return ERROR__STACK;
  }

  ++sd->dlev;
  streamlev *lev = sd->lev + sd->dlev;
  bzero(lev, sizeof(streamlev));
  lev->type = t->type;  // opening reason

  // setup level so parser never has to see PAREN, ARRAY etc
  switch (t->type) {
    case TOKEN_BRACE:
      if (!token_is_dict(lev - 1)) {
        lev->prev1.type = TOKEN_SEMICOLON;
        break;
      }
      lev->is_dict = 1;
      // fall-through
    case TOKEN_PAREN:
    case TOKEN_ARRAY:
    case TOKEN_T_BRACE:
      lev->prev1.type = TOKEN_COMMA;
  }

  return 1;
}

static int validate_close(int type, char close) {
  switch (type) {
    case TOKEN_BRACE:
    case TOKEN_T_BRACE:
      return close == '}';

    case TOKEN_PAREN:
      return close == ')';

    case TOKEN_ARRAY:
      return close == ']';
  }

  return 0;
}

static int stream_next(streamdef *sd, token *curr, token *next) {
  streamlev *lev = sd->lev + sd->dlev;  // always starting lev

  // handle pop stack
  if (curr->type == TOKEN_CLOSE) {
    if (!sd->dlev) {
      return ERROR__STACK;
    } else if (!validate_close(lev->type, curr->p[0])) {
      return ERROR__CLOSE;
    }
    --sd->dlev;
    return 0;
    // TODO(samthor): pop execution state
  }

  // handle descend into stack
  int mod = stack_mod(sd, curr);
  if (mod < 0) {
    return mod;  // err
  } else if (mod) {
    return 0;
  }

  // dictionary mode for non-lit
  if (lev->is_dict) {
    if (lev->is_dict_right) {
      // right side of dict, comma moves us back to left state
      if (curr->type == TOKEN_COMMA) {
        lev->is_dict_right = 0;
        return 0;
      }
    } else {
      // left side of dict, colon moves us to right state
      if (curr->type == TOKEN_COLON) {
        lev->is_dict_right = 1;
      } else if (curr->type == TOKEN_OP && curr->len == 1 && curr->p[0] == '*') {
        // TODO(samthor): next function is generator
      }
    }
  }

  // we mostly care about changing lit to keyword/symbol
  if (curr->type != TOKEN_LIT) {
    return 0;
  }

  if (lev->is_dict && !lev->is_dict_right) {
    // right-side of dict, allow "get" "set" "async" etc
    if (is_getset(curr->p, curr->len) || is_async(curr->p, curr->len)) {
      switch (next->type) {
        case TOKEN_PAREN:  // {get() {}}
        case TOKEN_COLON:  // {get: 1}
        case TOKEN_COMMA:  // {get,set}
        case TOKEN_BRACE:  // {get}
          break;

        default:
          return TOKEN_KEYWORD;
      }
    }

    return TOKEN_SYMBOL;
  }

  // "let" is either a keyword or symbol depending on use
  // TODO: what is to the left of us?
  if (token_string(curr, "let", 3)) {
    if (next->type == TOKEN_BRACE || next->type == TOKEN_ARRAY) {
      // e.g. "let[..]" or "let{..}", destructuring
      return TOKEN_KEYWORD;
    }

    if (next->type == TOKEN_LIT) {
      if (!token_string(next, "in", 2) && !token_string(next, "instanceof", 10)) {
        // "let instanceof Foo", for symbol use
        sd->type_next = TOKEN_OP;
        return TOKEN_KEYWORD;
      }
    }

    // otherwise, anything else is "let.foo" or whatever
    return TOKEN_SYMBOL;
  }

  if (lev->is_dict) {
    // few keywords are allowed here (depending on L/R)
    return 0;
  }

  if (next->line_no != curr->line_no) {
    if (is_asi_change(curr->p, curr->len) || is_label_keyword(curr->p, curr->len)) {
      sd->insert_asi = 1;
      return TOKEN_KEYWORD;
    }
  }

  // TODO(samthor): Lots of cases where this isn't true.
  // if (curr->lit_next_colon) {
  //   curr->type = TOKEN_LABEL;
  // }

  return 0;
}

int prsr_has_value(streamdef *sd) {
  streamlev *lev = sd->lev + sd->dlev;
//  printf(";;check;; got prev2=`%.*s` (%d) prev1=`%.*s` (%d)\n", lev->prev2.len, lev->prev2.p, lev->prev2.type, lev->prev1.len, lev->prev1.p, lev->prev1.type);

  if (lev->is_dict) {
    if (!lev->is_dict_right) {
      return 1;  // pretend that `{/foo: bar}` is division (spec insists), even though invalid
    }
    // otherwise, use normal logic
  }

  // catchall for simple cases: semicolon, comma etc
  if (type_implies_statement(lev->prev1.type)) {
    return 0;
  }

  switch (lev->prev1.type) {
    case TOKEN_PAREN:
      // e.g. "if (abc) /foo/"
      return lev->prev2.type == TOKEN_KEYWORD &&
          !is_control_paren(lev->prev2.p, lev->prev2.len);

    case TOKEN_LABEL:
      // should never happen, LABEL is from lookahead to find :
      return -1;

    default:
      printf(";;check;; got unhandled type=%d\n", lev->prev1.type);
    case TOKEN_ARRAY:
    case TOKEN_STRING:
    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
    case TOKEN_SYMBOL:
      return 1;

    case TOKEN_BRACE:
      break;
  }

  // brace case
  if (lev->is_dict) {
    return 1;  // was a dict, has value
  }

  switch (lev->prev2.type) {
    case TOKEN_ARROW:
      return 1;  // `... => {}` has value

    case TOKEN_PAREN:
      // p2=() p1={} <X>
      // TODO(samthor): Deviate from sweet-js. Store class/function-ness?
      break;
  }

  return 0;
}

int prsr_stream_next(streamdef *sd, token *curr, token *next) {
  if (sd->insert_asi) {
    streamlev *lev = sd->lev + sd->dlev;
    lev->prev2 = lev->prev1;
    lev->prev1 = (token) {.type = TOKEN_SEMICOLON};

    sd->insert_asi = 0;
    return TOKEN_SEMICOLON;
  }

  if (curr->type == TOKEN_COMMENT) {
    return 0;  // don't process comment
  }

  // record *lev first, as we log prev1/prev2 on current level (not descendant)
  streamlev *lev = sd->lev + sd->dlev;

  int update_type = sd->type_next;  // zero or updated type
  int type = curr->type;            // always curr->type or update
  if (!update_type) {
    int ret = stream_next(sd, curr, next);
    if (ret < 0) {
      return ret;
    } else if (ret) {
      type = ret;
    }
    update_type = ret;
  }

  // don't record TOKEN_CLOSE in prev log
  if (type != TOKEN_CLOSE) {
    lev->prev2 = lev->prev1;
    lev->prev1 = *curr;
    lev->prev1.type = type;
  }

  return update_type;
}

streamdef prsr_stream_init() {
  streamdef sd;
  bzero(&sd, sizeof(sd));

  // pretend top-level started with a semicolon inside a brace
  streamlev *base = sd.lev;
  base->prev1.type = TOKEN_SEMICOLON;
  base->type = TOKEN_BRACE;

  return sd;
}