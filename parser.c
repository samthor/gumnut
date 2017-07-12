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

#define STATE__ZERO          -1   // zero stack state
#define STATE__BLOCK          0   // execution block
#define STATE__STATEMENT      1   // single statement only (i.e., optional {}'s)
#define STATE__STATEMENT_ONE  2   // single statement disallowing ,'s (i.e., => STATEMENT)
#define STATE__PARENS         3   // (...) block: on its own, call or def
#define STATE__ARRAY          4   // array or index into array
#define STATE__CONTROL        5   // generic control statement: xx (foo) {}
#define STATE__DO_WHILE       6   // we're a do-while
#define STATE__OBJECT         7
#define STATE__EXPECT_ID      8
#define STATE__EXPECT_LABEL   9
#define STATE__MUST_WHILE     10
#define STATE__ARROW          11
#define STATE__HOIST          12
#define STATE__MUST_COLON     13

// flags for default states
#define FLAG__VALUE           1
#define FLAG__RESTRICT        2
#define FLAG__INITIAL         4    // allows 'var' etc, as well as function/class statements

// flags for do-while
#define FLAG__SEEN_WHILE      8   // inside do-while, seen 'while'

// flags for hoist
#define FLAG__CLASS           16  // class, not function

// flags for object
#define FLAG__RIGHT           32  // right side of object
#define FLAG__SEEN_MODIFIER   64  // seen 'get' or 'set'


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
  for (;;) {
    int was = p->curr->state;
    --p->curr;
    if (p->curr == p->stack) {
      return -1;
    }

    switch (p->curr->state) {
      case STATE__STATEMENT:
      case STATE__STATEMENT_ONE:
        continue;  // dec again, statement is over
      case STATE__ARROW:
        continue;  // dec again, arrow is over
      case STATE__CONTROL:
        if (was == STATE__PARENS) {
          stack_inc(p, STATE__STATEMENT, FLAG__INITIAL);  // seen parens, next is statement
        } else if (was == STATE__STATEMENT) {
          continue;  // dec again, control is over
        } else {
          return ERROR__UNEXPECTED;
        }
      case STATE__DO_WHILE:
        if (was == STATE__STATEMENT) {
          stack_inc(p, STATE__MUST_WHILE, 0);  // seen statement, next must be while
        } else if (was == STATE__MUST_WHILE) {
          p->curr->flag |= FLAG__SEEN_WHILE;  // flag for chunk_inner to expect parens
        } else if (was == STATE__PARENS) {
          // FIXME: should force ASI, basically pretend to be a newline
          continue;  // dec again, do-while is over
        } else {
          return ERROR__UNEXPECTED;
        }
      case STATE__HOIST:
        if (was == STATE__STATEMENT || was == STATE__OBJECT) {
          continue;  // done
        }
    }
    return 0;
  }
}

int chunk_normal_semicolon(parserdef *p, token *out) {
  if (p->curr->state == STATE__STATEMENT || p->curr->state == STATE__STATEMENT_ONE) {
    stack_dec(p);  // close current single statement
  }
  if (p->curr->state == STATE__BLOCK) {
    p->curr->flag = FLAG__INITIAL;
    if (!out) {
      return ERROR__SYNTAX_ASI;  // this is an ASI, be helpful to caller
    }
  }
  return 0;
}

int chunk_lookahead_symbol(tokendef td) {
  for (;;) {
    token out;
    int ret = prsr_next_token(&td, 0, &out);
    if (ret) {
      return -1;
    } else if (out.type == TOKEN_COMMENT || out.type == TOKEN_NEWLINE) {
      continue;
    }
    return out.type;
  }
}

int chunk_inner(parserdef *p, token *out) {
  char *s = out->p;
  int len = out->len;

  // zero state
  switch (out->type) {
    case TOKEN_NEWLINE:
      if (p->curr->flag & FLAG__RESTRICT) {
        p->curr->flag &= ~FLAG__RESTRICT;
        return chunk_normal_semicolon(p, NULL);
      }
      // fall-through
    case TOKEN_COMMENT:
      return 0;
  }

  switch (p->curr->state) {
    case STATE__STATEMENT:
    case STATE__STATEMENT_ONE:
    case STATE__BLOCK:
    case STATE__PARENS:
    case STATE__ARRAY:
      break;  // secret tip about writing JS parsers: these are all just expression lists

    case STATE__OBJECT:
      printf("state_object doing stuff: %d (depth=%d)\n", out->type, p->curr - p->stack);
      if (out->type == TOKEN_COMMA) {
        printf("found reset\n");
        p->curr->flag = 0;
        return 0;
      } else if (out->type == TOKEN_BRACE && s[0] == '}') {
        return stack_dec(p);
      } else if (p->curr->flag & FLAG__RIGHT) {
        // do nothing, just don't look for lits/colon
      } else if (out->type == TOKEN_LIT) {
        if (!(p->curr->flag & FLAG__SEEN_MODIFIER) && is_getset(s, len)) {
          // matched 'get' or 'set', return as keyword
          printf("found getset\n");
          out->type = TOKEN_KEYWORD;
          p->curr->flag |= FLAG__SEEN_MODIFIER;
        } else {
          // otherwise it's always a symbol
          printf("found symbol\n");
          out->type = TOKEN_SYMBOL;
        }
        return 0;
      } else if (out->type == TOKEN_COLON) {
        printf("switch to right\n");
        p->curr->flag = FLAG__RIGHT;
        return 0;
      }

      // give up and just look for a statement
      if (p->curr->flag & FLAG__RIGHT) {
        printf("llooking for STATEMENT_ONE\n");
        stack_inc(p, STATE__STATEMENT_ONE, 0);
      }
      break;

    case STATE__CONTROL:
      if (out->type == TOKEN_PAREN && s[0] == '(') {
        // allow FLAG__INITIAL inside control block (e.g. "for (var ...)")
        return stack_inc(p, STATE__PARENS, FLAG__INITIAL);
      }

      // otherwise, pretend we saw it
      stack_inc(p, STATE__PARENS, 0);
      stack_dec(p);
      break;

    case STATE__DO_WHILE:
      if (p->curr->flag & FLAG__SEEN_WHILE) {
        if (out->type != TOKEN_PAREN || s[0] != '(') {
          return ERROR__UNEXPECTED;
        }
      } else {
        stack_inc(p, STATE__STATEMENT, FLAG__INITIAL);
      }
      break;

    case STATE__MUST_WHILE:
      stack_dec(p);
      if (out->type != TOKEN_LIT || len != 5 || memcmp(s, "while", 5)) {
        return ERROR__UNEXPECTED;
      }
      out->type = TOKEN_SYMBOL;
      return 0;

    case STATE__HOIST:
      if (out->type == TOKEN_BRACE && s[0] == '{') {
        if (p->curr->flag & FLAG__CLASS) {
          return stack_inc(p, STATE__OBJECT, 0);
        }
        stack_inc(p, STATE__STATEMENT, FLAG__INITIAL);
        break;  // let rest of fn take care of STATEMENT
      }

      if (!(p->curr->flag & FLAG__CLASS)) {
        if (out->type == TOKEN_PAREN && s[0] == '(') {
          return stack_inc(p, STATE__PARENS, 0);
        }
      }

      if (out->type == TOKEN_OP) {
        return 0;  // TODO: could check for 'function *' here
      }
      if (out->type == TOKEN_LIT) {
        if (len == 7 && !memcmp(s, "extends", 7)) {
          out->type = TOKEN_KEYWORD;
          return stack_inc(p, STATE__STATEMENT_ONE, 0);
        }
        out->type = TOKEN_SYMBOL;  // got the function/class name
        return 0;
      }

      return ERROR__UNEXPECTED;

    case STATE__MUST_COLON:
      stack_dec(p);
      if (out->type == TOKEN_COLON) {
        return 0;
      }
      return ERROR__UNEXPECTED;

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
      stack_inc(p, STATE__STATEMENT_ONE, FLAG__INITIAL);
      break;

    default:
      printf("got unhandled state: %d\n", p->curr->state);
      return ERROR__TODO;
  }

  // EOF case
  if (out->type == TOKEN_EOF) {
    // nb. we don't close STATE__STATEMENT_* here, they're not valid candidates for EOF ASI
    if (p->curr->state != STATE__BLOCK || p->curr != (p->stack + 1)) {
      return ERROR__STACK;
    } else if (!(p->curr->flag & FLAG__INITIAL)) {
      return chunk_normal_semicolon(p, NULL);
    }
    return 0;
  }

  // replace 'in' and 'instanceof' with op
  if (out->type == TOKEN_LIT && is_op_keyword(s, len)) {
    out->type = TOKEN_OP;
  } else if (out->type == TOKEN_OP && is_double_addsub(s, len)) {
    // ++/--: if flag is a value; op in a block and after a newline; maybe yield an ASI
    if ((p->curr->flag & FLAG__VALUE) && (p->prev_type == TOKEN_NEWLINE)) {
      return chunk_normal_semicolon(p, NULL);
    }
    return 0;  // the addsub doesn't change flags anyway
  }

  int flag = p->curr->flag;
  p->curr->flag = 0;

  // non-literal tokens
  switch (out->type) {
    case TOKEN_COMMA:
      if (p->curr->state == STATE__STATEMENT_ONE) {
        return stack_dec(p);
      }
      // fall-through

    case TOKEN_TERNARY:
    case TOKEN_COLON:
    case TOKEN_OP:
      return 0;

    case TOKEN_SEMICOLON:
      return chunk_normal_semicolon(p, out);

    case TOKEN_STRING:
    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
      p->curr->flag = FLAG__VALUE;
      return 0;

    case TOKEN_ARROW:
      p->curr->flag = FLAG__VALUE;
      return stack_inc(p, STATE__ARROW, 0);

    case TOKEN_PAREN:
      if (s[0] == '(') {
        return stack_inc(p, STATE__PARENS, 0);
      } else if (p->curr->state == STATE__PARENS) {
        return stack_dec(p);
      }
      return ERROR__UNEXPECTED;

    case TOKEN_ARRAY:
      if (s[0] == '[') {
        return stack_inc(p, STATE__ARRAY, 0);
      } else if (p->curr->state == STATE__ARRAY) {
        return stack_dec(p);
      }
      return ERROR__UNEXPECTED;

    case TOKEN_BRACE:
      if (s[0] == '{') {
        // this is a block if we're the first expr (orphaned block)
        if (flag & FLAG__INITIAL) {
          p->curr->flag = FLAG__INITIAL;  // reset for after
          return stack_inc(p, STATE__BLOCK, FLAG__INITIAL);
        }
        return stack_inc(p, STATE__OBJECT, 0);
      } else if (p->curr->state == STATE__BLOCK && p->curr > p->stack) {
        // only look for BLOCK here
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
    if (s[0] == 'r' || s[0] == 't') {  // return, throw
      p->curr->flag |= FLAG__RESTRICT;
    }
    out->type = TOKEN_KEYWORD;
    return 0;
  }
  // FIXME: for some reason everything but "let" is still a keyword even in invalid places.

  // if this was a value, and we had a newline, emit ASI
  if (!(flag & FLAG__INITIAL) && (flag & FLAG__VALUE) && p->prev_type == TOKEN_NEWLINE) {
    return chunk_normal_semicolon(p, NULL);
  }

  // next token should also be part of expr
  if (is_expr_keyword(s, len)) {
    out->type = TOKEN_KEYWORD;
    return 0;
  }

  // TODO: look for 'async' and lookahead +1 for 'function'
  // nb. the "async () => {}" case is probably not gonna happen? mucho lookaheado

  // found a control keyword
  if (is_control_keyword(s, len)) {
    out->type = TOKEN_KEYWORD;
    p->curr->flag = FLAG__INITIAL;  // reset for after
    if (s[0] == 'd') {
      return stack_inc(p, STATE__DO_WHILE, 0);
    }
    return stack_inc(p, STATE__CONTROL, 0);
  } else if (is_hoist_keyword(s, len)) {
    if (flag & FLAG__INITIAL) {
      p->curr->flag |= FLAG__INITIAL;  // reset for after
    } else {
      p->curr->flag |= FLAG__VALUE;
    }
    out->type = TOKEN_KEYWORD;
    return stack_inc(p, STATE__HOIST, s[0] == 'c' ? FLAG__CLASS : 0);
  } else if (is_label_keyword(s, len)) {
    p->curr->flag |= FLAG__RESTRICT;
    out->type = TOKEN_KEYWORD;
    return stack_inc(p, STATE__EXPECT_LABEL, 0);
  } else if (is_asi_keyword(s, len)) {
    // this should just catch 'yield'
    p->curr->flag |= FLAG__RESTRICT;
    out->type = TOKEN_KEYWORD;
    return 0;
  }

  // look for initial label-like cases
  if (flag & FLAG__INITIAL) {
    if (len == 4 && !memcmp(s, "case", 4)) {
      p->curr->flag |= FLAG__INITIAL;  // reset for after
      out->type = TOKEN_KEYWORD;
      printf("got TODO in case\n");
      // TODO: look for STATEMENT but ending at :
      return ERROR__TODO;
    }
    if (len == 7 && !memcmp(s, "default", 7)) {
      p->curr->flag |= FLAG__INITIAL;  // reset for after
      out->type = TOKEN_KEYWORD;
      return stack_inc(p, STATE__MUST_COLON, 0);
    }
    int next_type = chunk_lookahead_symbol(p->td);
    if (next_type == TOKEN_COLON) {
      p->curr->flag |= FLAG__INITIAL;  // reset for after
      out->type = TOKEN_LABEL;
      return stack_inc(p, STATE__MUST_COLON, 0);
    }
  }

  p->curr->flag |= FLAG__VALUE;
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
  int ret = prsr_next_token(&p->td, p->curr->flag & FLAG__VALUE, out);
  if (ret) {
    return ret; 
  }

  // actual chunk call
  ret = chunk_inner(p, out);
  if (ret) {
    if (ret != ERROR__SYNTAX_ASI) {

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

int prsr_parser_init(parserdef *p, char *buf) {
  bzero(p, sizeof(parserdef));
  p->td = prsr_init_token(buf);
  p->stack[0].state = STATE__ZERO;
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
