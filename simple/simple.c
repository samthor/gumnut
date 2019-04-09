
#include <string.h>
#include "simple.h"
#include "../utils.h"

#define SSTACK__BLOCK 1  // block execution context
#define SSTACK__GROUP 2  // group execution context
#define SSTACK__FUNC  3  // expects upcoming "() {}"

// context are set on all statements
#define CONTEXT__STRICT    1
#define CONTEXT__ASYNC     2
#define CONTEXT__GENERATOR 4


typedef struct {
  token t1;
  token t2;
  uint8_t stype : 3;
  uint8_t context : 3;
} sstack;


typedef struct {
  tokendef *td;
  token *next;  // convenience
  token tok;
  int tok_has_value;  // has_value current tok was read with

  prsr_callback cb;
  void *arg;

  sstack *curr;
  sstack stack[256];
} simpledef;


static int token_string(token *t, char *s, int len) {
  return t->len == len && !memcmp(t->p, s, len);
}


// stores a virtual token in the stream
static void record_virt(simpledef *sd, int type) {
  token *t = &(sd->curr->t1);
  sd->curr->t2 = *t;  // move t1 => t2
  bzero(t, sizeof(token));
  t->type = type;
}


// // force-yields this token and records it
// static void yield_tok(simpledef *sd) {
//   if (!sd->tok.p) {
//     return;  // can't record or already recorded
//   }
//   sd->cb(sd->arg, &(sd->))
//   sd->curr->t2 = sd->curr->t1;
//   sd->curr->t1 = sd->tok;
// }


// stores a virtual token in the stream, and yields it before the current token
static void yield_virt(simpledef *sd, int type) {
  record_virt(sd, type);
  sd->cb(sd->arg, &(sd->curr->t1));
}


// places the next useful token in sd->tok, yielding previous current
static int skip_walk(simpledef *sd, int has_value) {
  if (sd->tok.p) {
    sd->cb(sd->arg, &(sd->tok));
  }
  for (;;) {
    // prsr_next_token can reveal comments, loop until over them
    int out = prsr_next_token(sd->td, &(sd->tok), has_value);
    if (out || sd->tok.type != TOKEN_COMMENT) {
      sd->tok_has_value = has_value;
      return out;
    }
    sd->cb(sd->arg, &(sd->tok));
  }
}


// records/yields the current token, places the next useful token
static int record_walk(simpledef *sd, int has_value) {
  sd->curr->t2 = sd->curr->t1;
  sd->curr->t1 = sd->tok;
  return skip_walk(sd, has_value);
}


static int is_optional_keyword(sstack *dep) {
  uint8_t mask = 0;

  if (token_string(&(dep->t1), "await", 5)) {
    mask = CONTEXT__ASYNC;
  } else if (token_string(&(dep->t1), "yield", 5)) {
    mask = CONTEXT__GENERATOR;
  } else {
    return 0;
  }
  // FIXME: strict mode keywords

  return dep->context & mask;
}


// whether the current position has a function decl/stmt
static int peek_function(simpledef *sd) {
  if (sd->tok.type != TOKEN_LIT) {
    return 0;
  } else if (token_string(&(sd->tok), "async", 5)) { 
    return sd->next->type == TOKEN_LIT && token_string(sd->next, "function", 8);
  }
  return token_string(&(sd->tok), "function", 8);
}


// matches any current function decl/stmt
static int match_function(simpledef *sd) {
  if (!peek_function(sd)) {
    return -1;
  }

  uint8_t context = (sd->curr->context & CONTEXT__STRICT);
  if (sd->tok.p[0] == 'a') {
    context |= CONTEXT__ASYNC;
    sd->tok.type = TOKEN_KEYWORD;
    skip_walk(sd, 0);  // consume "async"
  }
  sd->tok.type = TOKEN_KEYWORD;
  record_walk(sd, 0);  // consume "function"

  // optionally consume generator star
  if (sd->tok.type == TOKEN_OP && token_string(&(sd->tok), "*", 1)) {
    skip_walk(sd, 0);
    context |= CONTEXT__GENERATOR;
  }

  // optionally consume function name
  if (sd->tok.type == TOKEN_LIT) {
    sd->tok.type = TOKEN_SYMBOL;
    skip_walk(sd, 0);  // consume name
  }

  return context;
}


static int is_use_strict(token *t) {
  if (t->type != TOKEN_STRING || t->len != 12) {
    return 0;
  }
  return !memcmp(t->p, "'use strict'", 12) || !memcmp(t->p, "\"use strict\"", 12);
}


static sstack *stack_inc(simpledef *sd, uint8_t stype) {
  // TODO: check bounds
  ++sd->curr;
  bzero(sd->curr, sizeof(sstack));
  sd->curr->stype = stype;
  sd->curr->context = (sd->curr - 1)->context;  // copy context
  return sd->curr;
}


static int stack_has_value(sstack *dep) {
  switch (dep->t1.type) {
    case TOKEN_EOF:
      return 0;

    case TOKEN_INTERNAL:
      return 1;  // if this was a decl, then TOKEN_INTERNAL wouldn't be in stream

    case TOKEN_PAREN:
      if (dep->t2.type == TOKEN_LIT && is_control_paren(dep->t2.p, dep->t2.len)) {
        return 0;
      }
      return 1;

    case TOKEN_BRACE:
      if (!(dep + 1)->stype || !dep->stype) {
        return 1;  // prev was dict OR we are non-block
      }
      return 0;

    case TOKEN_LIT:
      if (is_optional_keyword(dep)) {
        return 0;  // async or await are valid, they have no value
      }
      return !is_always_operates(dep->t1.p, dep->t1.len);

    case TOKEN_STRING:
    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
      return 1;
  }

  return 0;
}


static int simple_consume(simpledef *sd) {

  // function state, allow () or {}
  if (sd->curr->stype == SSTACK__FUNC) {
    if (sd->tok.type == TOKEN_PAREN) {
      // allow "function ()"
      record_walk(sd, 0);
      stack_inc(sd, SSTACK__GROUP);
      return 0;
    } else if (sd->tok.type == TOKEN_BRACE) {
      // terminal state of func, pop and insert normal block
      uint8_t context = sd->curr->context;
      --sd->curr;
      record_walk(sd, 0);
      stack_inc(sd, SSTACK__BLOCK);
      sd->curr->context = context;
      return 0;
    }

    // invalid, abandon function def
    --sd->curr;
  }

  // zero state, determine what to push
  if (sd->curr->stype == SSTACK__BLOCK) {

    // match conditional
    if (token_string(&(sd->tok), "do", 2)) {
      if (sd->td->next.type == TOKEN_BRACE) {
        record_walk(sd, -1);
        stack_inc(sd, SSTACK__BLOCK);
      }
    }

    // match anonymous block
    if (sd->tok.type == TOKEN_BRACE) {
      printf("got anon block\n");
      record_walk(sd, 0);
      stack_inc(sd, SSTACK__BLOCK);
      return 0;
    }

    // match label
    if (sd->tok.type == TOKEN_LIT && sd->next->type == TOKEN_COLON) {
      if (is_reserved_word(sd->tok.p, sd->tok.len)) {
        // FIXME: this is an error
        sd->tok.type = TOKEN_KEYWORD;
      } else {
        sd->tok.type = TOKEN_LABEL;
      }
      skip_walk(sd, -1);  // consume label
      skip_walk(sd, -1);  // consume colon
      return 0;
    }

    // match 'use strict';
    if (sd->curr->t1.type == TOKEN_EOF && is_use_strict(&(sd->tok))) {
      // FIXME: can't be e.g. `'use strict'.length`, must be single value
      // ... could peek or check when statement is 'done'
      sd->curr->context |= CONTEXT__STRICT;
    }

    // match hoisted function (don't insert a regular statement first)
    int context = match_function(sd);
    if (context >= 0) {
      stack_inc(sd, SSTACK__FUNC);
      sd->curr->context = context;
      return 0;
    }

    // TODO: match hoisted class

    // ... or start a regular statement
    stack_inc(sd, 0);
  }

  // match statements
  switch (sd->tok.type) {
    case TOKEN_SEMICOLON:
      if (!sd->curr->stype) {
        --sd->curr;
      }
      return record_walk(sd, 0);

    case TOKEN_COMMA:
      // relevant in "async () => blah, foo", steal from parent
      sd->curr->context = (sd->curr - 1)->context;
      return record_walk(sd, 0);

    case TOKEN_ARROW:
      if (!(sd->curr->t1.type == TOKEN_PAREN || sd->curr->t1.type == TOKEN_LIT)) {
        // not a valid arrow func, treat as op
        return record_walk(sd, 0);
      }

      uint8_t context = (sd->curr->context & CONTEXT__STRICT);
      if (token_string(&(sd->curr->t2), "async", 5)) {
        int type = sd->curr->t2.type;
        if (type == TOKEN_LIT || type == TOKEN_KEYWORD) {
          context |= CONTEXT__ASYNC;
        }

        if (type == TOKEN_LIT) {
          // nb. we can now be confident this _was_async, announce
          sd->curr->t2.type = TOKEN_KEYWORD;
          // FIXME: allow testing/other to deal with out-of-order
          // sd->cb(sd->arg, &(sd->curr->t2));
        }
      }

      if (sd->td->next.type == TOKEN_BRACE) {
        // the sensible arrow function case, with a proper body
        // e.g. "() => { statements }"
        record_walk(sd, -1);
        stack_inc(sd, SSTACK__BLOCK);
      } else {
        // just change statement's context (e.g. () => async () => () => ...)
        record_walk(sd, 0);
      }
      sd->curr->context = context;
      return 0;

    case TOKEN_EOF:
    case TOKEN_CLOSE:
      if (sd->curr->stype == 0) {
        if ((sd->curr - 1)->stype == SSTACK__BLOCK && sd->curr->t1.type != TOKEN_EOF) {
          // closing a statement inside brace, yield ASI
          // FIXME(samthor): remove later, do this differently
          yield_virt(sd, TOKEN_SEMICOLON);
        }
        --sd->curr;  // finish pending value
      }

      if (sd->curr->stype != SSTACK__BLOCK && sd->curr->stype != SSTACK__GROUP) {
        printf("got unknown stype at TOKEN_CLOSE: %d\n", sd->curr->stype);
        return 1;
      }
      --sd->curr;  // finish block or group (at EOF this pops to zero stack)

      // if we're in a regular statement, this stack has value
      int has_value = (sd->curr->stype == 0);
      if (sd->curr->t1.type == TOKEN_TERNARY) {
        has_value = 0; // ... not if "? ... :"
      }

      if (sd->tok.type != TOKEN_EOF) {
        // noisy for EOF, where we don't care /shrug
        printf("popped stack (%ld) in op? has_value=%d\n", sd->curr - sd->stack, has_value);
      }
      return skip_walk(sd, has_value);

    case TOKEN_BRACE:    // always dict here (group)
    case TOKEN_TERNARY:
    case TOKEN_ARRAY:
    case TOKEN_PAREN:
    case TOKEN_T_BRACE:
      record_walk(sd, 0);
      stack_inc(sd, SSTACK__GROUP);
      return 0;

    case TOKEN_OP: {
      int has_value = 0;
      if (is_double_addsub(sd->tok.p, sd->tok.len)) {
        // ++ or -- don't change value-ness
        has_value = sd->tok_has_value;
      }
      return record_walk(sd, has_value);
    }

    case TOKEN_STRING:
    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
      return record_walk(sd, 1);

    case TOKEN_LIT:
      break;

    default:
      return 0;

  }

  int context = match_function(sd);
  if (context >= 0) {
    // non-hoisted function
    stack_inc(sd, SSTACK__FUNC);
    sd->curr->context = context;
    return 0;
  }

  // FIXME: match keywords
  record_walk(sd, 1);
  return 0;
}


int prsr_simple(tokendef *td, prsr_callback cb, void *arg) {
  simpledef sd;
  bzero(&sd, sizeof(simpledef));
  sd.curr = sd.stack + 1;
  sd.td = td;
  sd.next = &(td->next);
  sd.cb = cb;
  sd.arg = arg;

  sd.curr->stype = SSTACK__BLOCK;
  record_walk(&sd, 0);

  int ret = 0;
  for (;;) {
    char *prev = sd.tok.p;
    ret = simple_consume(&sd);

    if (!ret && prev == sd.tok.p) {
      // simple_consume didn't eat the token, consume it for them
      printf("simple_consume didn't consume: %d %.*s\n", sd.tok.type, sd.tok.len, sd.tok.p);
      ret = record_walk(&sd, -1);
    }

    if (ret) {
      break;
    }
    if (sd.tok.type == TOKEN_EOF) {
      ret = simple_consume(&sd);  // last run, run EOF to close statements
      break;
    }
  }

  if (ret) {
    return ret;
  } else if (sd.tok.type != TOKEN_EOF) {
    return ERROR__CLOSE;
  } else if (sd.curr != sd.stack) {
    printf("stack is %ld too high\n", sd.curr - sd.stack);
    return ERROR__STACK;
  }

  return 0;
}