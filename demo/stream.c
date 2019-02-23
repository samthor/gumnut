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

// stream: internal modes
#define _STREAM_INIT      0
#define _STREAM_LABEL     25
#define _STREAM_FUNCTION  26
#define _STREAM_HOIST     27
#define _STREAM_DICT      28
#define _STREAM_VALUE     29
#define _STREAM_CONTROL   30  // `()` part of control, e.g. "if ()"
#define _STREAM_STATEMENT 31

static int lev_follows_control(streamlev *lev) {
  if (lev->prev1.type == TOKEN_PAREN && lev->prev2.type == TOKEN_KEYWORD) {
    return is_control_paren(lev->prev2.p, lev->prev2.len);
  }
  return 0;
}

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

  // setup level so parser never looks back on start PAREN, ARRAY etc
  switch (t->type) {
    case TOKEN_BRACE:
      if (!token_is_dict(lev - 1)) {
        lev->mode = _MODE__DEFAULT;
        break;
      }
      lev->mode = _MODE__DICT;
      // fall-through
    case TOKEN_PAREN:
    case TOKEN_ARRAY:
    case TOKEN_T_BRACE:
      lev->mode = _MODE_VALUE;
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

static int stream_next_lit(streamdef *sd, token *curr, token *next) {
  streamlev *lev = sd->lev + sd->dlev;  // always starting lev

  if (lev->is_dict) {
    // block dict

    if (!lev->is_dict_right) {
      // right-side of dict, allow "get" "set" "async" etc
      if (is_getset(curr->p, curr->len) || token_string(curr, "async", 5)) {
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

  } else if (lev->type == TOKEN_BRACE) {
    // brace mode (begin statement in regular brace mode)

    int statement_begin =
        lev->prev1.type == TOKEN_ARROW ||
        lev->prev1.type == TOKEN_SEMICOLON ||
        lev_follows_control(lev) || 
        (lev->prev1.type == TOKEN_BRACE && !(lev + 1)->is_dict);

    if (statement_begin) {
      // this is a new statement
      // TODO(samthor): we should probably only be here at all if it's new
      printf("got new statement\n");
    } else if (curr->line_no != lev->prev1.line_no) {
      // .. but might also be a new statement if invalid grammar
    }

    if (!lev->statement) {
      if (is_hoist_keyword(curr->p, curr->len)) {
//        lev->statement = _START__HOIST;
      }
    }

    // "let" is either a keyword or symbol depending on use
    // TODO: what is to the left of us?
    if (token_string(curr, "let", 3)) {
      if (lev->prev1.type != TOKEN_SEMICOLON && !lev_follows_control(lev)) {
        return TOKEN_SYMBOL;  // not in an expected place for 'let'
      }

      if (next->type == TOKEN_BRACE || next->type == TOKEN_ARRAY) {
        // e.g. "let[..]" or "let{..}", destructuring
        return TOKEN_KEYWORD;
      }

      if (next->type == TOKEN_LIT) {
        if (is_op_keyword(next->p, next->len)) {
          // "let instanceof Foo", for symbol use
          sd->type_next = TOKEN_OP;
          return TOKEN_SYMBOL;
        }

        // variable declaration
        return TOKEN_KEYWORD;
      }

      // otherwise, anything else is "let.foo" or whatever
      return TOKEN_SYMBOL;
    }

    // look for "break foo;" or "continue bar;""
    if (is_label_keyword(curr->p, curr->len)) {
      if (curr->line_no != next->line_no) {
        sd->insert_asi = 1;
      } else if (next->type == TOKEN_LIT) {
        // mark next as label
        if (is_reserved_word(next->p, next->len)) {
          // nb. invalid grammar
          sd->type_next = TOKEN_KEYWORD;
        } else {
          sd->type_next = TOKEN_LABEL;
        }
      }
      return TOKEN_KEYWORD;
    }

    // look for "return \n {}" or "yield \n bar"
    if (is_asi_change(curr->p, curr->len)) {
      sd->insert_asi = (curr->line_no != next->line_no);
      if (curr->p[0] == 'y') {
        return TOKEN_OP;  // yield is oplike
      }
      return TOKEN_KEYWORD;
    }

    // try to match "label:"
    if (next->type == TOKEN_COLON) {
      if (lev->prev1.line_no == curr->line_no && !lev_follows_control(lev)) {
        // TODO(samthor): we should catch this in state machine
        goto skip_label_colon;
      }

      // TODO(samthor): we should catch this in state machine
      switch (lev->prev1.type) {
        case TOKEN_OP:
        case TOKEN_TERNARY:
        case TOKEN_DOT:
          goto skip_label_colon;
      }

      if (is_reserved_word(curr->p, curr->len)) {
        // nb. invalid grammar
        return TOKEN_KEYWORD;
      }
      return TOKEN_LABEL;
  skip_label_colon:
      ;  // empty statement
    }

  }

  // all execution modes here (block, paren, ...)

  // match ops
  if (is_oplike(curr->p, curr->len)) {
    return TOKEN_OP;
  }

  // match async
  if (token_string(curr, "async", 5)) {
    if (token_string(next, "function", 8)) {
      // TODO(samthor): start function state machine
      sd->type_next = TOKEN_KEYWORD;
      return TOKEN_KEYWORD;
    } else if (next->type == TOKEN_PAREN) {
      // FIXME: this is _likely_ to be a keyword. But not always. We have to
      // look forward for the => after the () section.
      return TOKEN_KEYWORD;
    }
  }

  // match other keywords "always keywords"
  if (is_always_keyword(curr->p, curr->len)) {
    return TOKEN_KEYWORD;
  }

  return TOKEN_SYMBOL;
}

static int smatch_decl(token *curr, token *next) {
  if (curr->type != TOKEN_LIT || !is_decl_keyword(curr->p, curr->len)) {
    return 0;
  }

  if (curr->p[0] != 'l') {
    // const or var
  } else if (next->type == TOKEN_BRACE || next->type == TOKEN_ARRAY) {
    // e.g. "let[..]" or "let{..}", destructuring
  } else if (next->type != TOKEN_LIT) {
    return 0;  // no following literal (e.g. "let = 1")
  } else if (is_op_keyword(next->p, next->len)) {
    // clear variable use ("let instanceof Foo" or "let in bar")
    next->type = TOKEN_OP;  // these are op-like
    return 0;
  }

  return 1;
}

/**
 * push type/mode onto stack
 */
static int stack_push(streamdef *sd, int type) {
  if (sd->dlev == __STACK_SIZE - 1) {
    return ERROR__STACK;
  }

  ++sd->dlev;
  streamlev *lev = sd->lev + sd->dlev;
  bzero(lev, sizeof(streamlev));
  lev->type = type;  // opening reason, zero/EOF for virtual
  return 0;
}

/**
 * pop until non-virtual mode on stack
 */
static int stack_pop(streamdef *sd) {
  if (!sd->dlev) {
    return ERROR__STACK;
  }
  --sd->dlev;
  return 0;
}

static int stream_next(streamdef *sd, token *curr, token *next) {
  streamlev *lev = sd->lev + sd->dlev;  // always starting lev
  int _retcheck;
#define _CHECK(v) {_retcheck = (v); if (_retcheck) {return _retcheck;}};
#define _RUN(_type) \
    for (int _h = 1; _h && lev->type == _type; _h = 0)

  int always_keyword = (curr->type == TOKEN_LIT && is_always_keyword(curr->p, curr->len));

  // zero block mode
  _RUN(_STREAM_INIT) {
    if (curr->type == TOKEN_CLOSE) {
      _CHECK(stack_pop(sd));

      // if this was a block statement (e.g. unattached "{}"), then break it too
      streamlev *up = lev - 1;
      if (up->type == _STREAM_STATEMENT) {
        return stack_pop(sd);
      }
      return 0;
    }

    // otherwise, consume a statement
    _CHECK(stack_push(sd, _STREAM_STATEMENT));
  }

  // label mode
  _RUN(_STREAM_LABEL) {
    if (!always_keyword && curr->type == TOKEN_LIT) {
      curr->type = TOKEN_LABEL;
    } else {
      _CHECK(stack_pop(sd));
    }
  }

  // statement mode
  _RUN(_STREAM_STATEMENT) {
    // unattached "{"
    if (curr->type == TOKEN_BRACE) {
      return stack_push(sd, _STREAM_INIT);
    }

    // only lit below
    if (curr->type != TOKEN_LIT) {
      break;
    }

    // "var x"
    if (smatch_decl(curr, next)) {
      curr->type = TOKEN_KEYWORD;
      return stack_push(sd, _STREAM_VALUE);
    }

    // "if (x)"
    if (is_control_paren(curr->p, curr->len)) {
      curr->type = TOKEN_KEYWORD;
      return stack_push(sd, _STREAM_CONTROL);
    }

    // "try {"
    if (is_block_creator(curr->p, curr->len)) {
      curr->type = TOKEN_KEYWORD;
      return stack_push(sd, _STREAM_STATEMENT);
    }

    // "function" or "class"
    if (is_hoist_keyword(curr->p, curr->len)) {
      curr->type = TOKEN_KEYWORD;
      _CHECK(stack_push(sd, _STREAM_HOIST));
      break;
    }

    // "async"
    if (token_string(curr, "async", 5)) {
      if (next->type == TOKEN_LIT && token_string(next, "function", 8)) {
        curr->type = TOKEN_KEYWORD;
        return stack_push(sd, _STREAM_HOIST);
      }
      break;
    }

    // "throw"
    if (token_string(curr, "throw", 5)) {
      curr->type = TOKEN_KEYWORD;
      return stack_push(sd, _STREAM_VALUE);
    }

    // "return"
    if (token_string(curr, "return", 6)) {
      curr->type = TOKEN_KEYWORD;
      if (next->line_no == curr->line_no) {
        return stack_push(sd, _STREAM_VALUE);  // effected by ASI
      }
      sd->insert_asi = 1;
      return 0;
    }

    // "continue" or "break foo"
    if (is_label_keyword(curr->p, curr->len)) {
      if (next->line_no == curr->line_no) {
        return stack_push(sd, _STREAM_LABEL);
      }
      sd->insert_asi = 1;
      return 0;
    }

    // look for labels
    if (!is_always_keyword && next->type == TOKEN_COLON) {

    }

    // otherwise, always start value
    _CHECK(stack_push(sd, _STREAM_VALUE));
  }

  // read a general value mode
  _RUN(_STREAM_VALUE) {

    if (curr->type == TOKEN_CLOSE) {
      return stack_pop(sd);
    }

    if (curr->type == TOKEN_BRACE) {
      return stack_push(sd, _STREAM_DICT);
    }

    if (curr->type == TOKEN_ARRAY || curr->type == TOKEN_PAREN || curr->type == TOKEN_T_BRACE) {
      return stack_push(sd, _STREAM_VALUE);
    }



  }

#undef _RUN
#undef _CHECK
}

int prsr_has_value(streamdef *sd) {
  streamlev *lev = sd->lev + sd->dlev;
  if (lev->type != _STREAM_VALUE) {
    return 0;
  }

  switch (sd->last.type) {
    case TOKEN_STRING:
    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
    case TOKEN_SYMBOL:
    case TOKEN_CLOSE:
      return 1;
  }

  return 0;
}

int prsr_stream_next(streamdef *sd, token *curr, token *next) {
  if (sd->insert_asi) {
    sd->last = (token) {.type = TOKEN_SEMICOLON};
    sd->insert_asi = 0;
    return TOKEN_SEMICOLON;
  }
  if (curr->type != TOKEN_COMMENT) {
    int ret = stream_next(sd, curr, next);
    if (ret) {
      return ret;
    }
    sd->last = *curr;
  }
  return 0;
}

streamdef prsr_stream_init() {
  streamdef sd;
  bzero(&sd, sizeof(sd));
  return sd;
}