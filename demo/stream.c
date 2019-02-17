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

streamdef prsr_stream_init() {
  streamdef sd;
  bzero(&sd, sizeof(sd));
  return sd;
}

static int token_is_block(streamlev *up, token *t) {
  // called when the current token is TOKEN_BRACE

  if (!up->prev1.type) {
    // e.g. `({`
    // nb. TOKEN_BRACE is included here, e.g. `{{`, but it's a syntax error
    return 0;
  }

  if (up->prev1.type == TOKEN_COLON && up->type == TOKEN_BRACE) {
    // e.g. `{foo:{`
    return up->is_block;
  }

  if (up->prev1.type == TOKEN_KEYWORD) {
    if (is_case(up->prev1.p, up->prev1.len)) {
      // e.g. `case {`
      return 0;
    } else if (is_asi_change(up->prev1.p, up->prev1.len)) {
      // e.g. `return {}` or `return \n {}`
      return up->prev1.line_no != t->line_no;
    }
    return 1;
  }

  switch (up->prev1.type) {
    case TOKEN_COMMA:
    case TOKEN_OP:
    case TOKEN_COLON:
    case TOKEN_TERNARY:
      return 0;
  }
  return 1;
}

static int stack_mod(streamdef *sd, token *t) {
  switch (t->type) {
    case TOKEN_BRACE:
    case TOKEN_ARRAY:
    case TOKEN_PAREN:
    case TOKEN_T_BRACE:
      break;

    case TOKEN_CLOSE:
      // assume correct, we can check later?
      if (!sd->dlev) {
        return -1;
      }
      --sd->dlev;
      // TODO: pop execution state
      // nb. don't change prev token, use left side
      return 0;

    default:
      return 0;
  }

  if (sd->dlev == __STACK_SIZE - 1) {
    return -1;
  }

  ++sd->dlev;
  streamlev *lev = sd->lev + sd->dlev;
  bzero(lev, sizeof(streamlev));
  lev->type = t->type;  // opening reason

  if (t->type == TOKEN_BRACE) {
    lev->is_block = token_is_block(lev - 1, t);
    printf("got brace, is_block=%d\n", lev->is_block);
  }
  return 1;
}

static int stream_next(streamdef *sd, token *out) {
  streamlev *lev = sd->lev + sd->dlev;  // always starting lev

  do {
    int mod = stack_mod(sd, out);
    if (mod < 0) {
      return mod;  // err
    } else if (mod) {
      break;  // modified up, do nothing
    } else if (out->type == TOKEN_CLOSE) {
      // modified down
      // TODO(samthor): check open/close matching here
      return 0;  // bail early, we don't record end token
    }

    if (out->type != TOKEN_LIT) {
      break;  // nothing to do for non-lit
      // TODO(samthor): generate ASI?
    }

    // TODO(samthor): Lots of cases where this isn't true.
    if (out->lit_next_colon) {
      out->type = TOKEN_LABEL;
    }

  } while (0);

  lev->prev2 = lev->prev1;
  lev->prev1 = *out;

  return 0;
}

int prsr_has_value(streamdef *sd) {
  streamlev *lev = sd->lev + sd->dlev;
  printf(";;check;; got prev2=`%.*s` prev1=`%.*s`\n", lev->prev2.len, lev->prev2.p, lev->prev1.len, lev->prev1.p);

  if (!lev->prev1.type) {
    // first token in section
    if (lev->type == TOKEN_BRACE) {
      // TODO(samthor): set this somewhere
      return lev->is_block;
    }
    return 0;
  }

  switch (lev->prev1.type) {
    case TOKEN_PAREN:
      // e.g. "if (abc) /foo/"
      return !is_control_paren(lev->prev2.p, lev->prev2.len);

    case TOKEN_LABEL:
      // should never happen, LABEL is from lookahead to find :
      return -1;

    case TOKEN_SEMICOLON:
    case TOKEN_COMMA:
    case TOKEN_SPREAD:
    case TOKEN_DOT:
    case TOKEN_OP:
    case TOKEN_ARROW:
    case TOKEN_COLON:
    case TOKEN_TERNARY:
    case TOKEN_KEYWORD:
      return 0;

    case TOKEN_BRACE:
      break;

    default:
      printf(";;check;; got unhandled type=%d\n", lev->prev1.type);
    case TOKEN_ARRAY:
    case TOKEN_STRING:
    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
    case TOKEN_SYMBOL:
      return 1;
  }

  // brace case
  if (!lev->is_block) {
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

int prsr_stream_next(streamdef *sd, token *out) {
  if (out->type == TOKEN_COMMENT) {
    return 0;  // don't process comment
  }

  int ret = stream_next(sd, out);
  if (ret) {
    return ret;
  }
  return 0;
}