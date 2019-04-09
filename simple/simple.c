
#include <string.h>
#include "simple.h"
#include "../utils.h"

#define SSTACK__BLOCK 1  // block execution context
#define SSTACK__GROUP 2  // group execution context
#define SSTACK__FUNC  3  // expects upcoming "() {}"
#define SSTACK__DECL  4  // discard implict value when done
#define SSTACK__DO_WHILE 5

// context are set on all statements
#define CONTEXT__STRICT    1
#define CONTEXT__ASYNC     2
#define CONTEXT__GENERATOR 4


typedef struct {
  token t1;
  token t2;
  uint8_t stype : 3;
  uint8_t context : 3;
  uint8_t is_value : 1;
} sstack;


typedef struct {
  tokendef *td;
  token *next;  // convenience
  token tok;

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

  uint8_t context = (sd->curr && CONTEXT__STRICT);
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
      --sd->curr;
      skip_walk(sd, 0);
      record_virt(sd, TOKEN_VALUE);
      stack_inc(sd, SSTACK__BLOCK);
      return 0;
    }

    // invalid, abandon function def
    --sd->curr;
  }

  // finished a decl, pop
  if (sd->curr->stype == SSTACK__DECL) {
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

    // match hoisted class/function
    if (peek_function(sd) ||
        (sd->tok.type == TOKEN_LIT && token_string(&(sd->tok), "class", 5))) {
      stack_inc(sd, SSTACK__DECL);
    }

    // ... or start a regular statement
    stack_inc(sd, 0);
  }

  // match statements
  switch (sd->tok.type) {
    case TOKEN_ARROW:
      if (!(sd->curr->t1.type == TOKEN_PAREN || sd->curr->t1.type == TOKEN_LIT)) {
        record_walk(sd, 0);
        return 0;  // not a valid construct
      }

      uint8_t context = (sd->curr->context & CONTEXT__STRICT);
      if (sd->curr->t2.type == TOKEN_LIT && token_string(&(sd->curr->t2), "async", 5)) {
        context |= CONTEXT__ASYNC;
      }

      if (sd->td->next.type == TOKEN_BRACE) {
        // the sensible arrow function case, with a proper body
        // e.g. "() => { statements }"
        record_walk(sd, -1);
        stack_inc(sd, SSTACK__BLOCK);
        sd->curr->is_value = 1;
        sd->curr->context = context;
        return 0;
      }

      // ... otherwise, change our statement's context (e.g. () => async () => () => ...)
      sd->curr->context = context;
      record_walk(sd, 0);
      return 0;

    case TOKEN_EOF:
      if (sd->curr == sd->stack) {
        return 0;
      }
      // fall-through (while sd->curr > sd->stack)

    case TOKEN_CLOSE:
      if (sd->curr->stype == 0) {
        if ((sd->curr - 1)->stype == SSTACK__BLOCK && (sd->curr - 2)->stype != SSTACK__DECL) {
          // closing a statement inside brace, yield ASI
          yield_virt(sd, TOKEN_SEMICOLON);
        }
        --sd->curr;  // finish pending value
      }

      if (sd->curr->stype != SSTACK__BLOCK && sd->curr->stype != SSTACK__GROUP) {
        printf("got unknown stype at TOKEN_CLOSE: %d\n", sd->curr->stype);
        return 1;
      }
      --sd->curr;  // finish block or group (at EOF this pops to zero stack)

      // the token starting us is always TOKEN_VALUE, or not
      int has_value = (sd->curr->t1.type == TOKEN_VALUE);
      skip_walk(sd, has_value);
      return 0;

    case TOKEN_BRACE:
      stack_inc(sd, SSTACK__GROUP);  // TODO: not just group
      return 0;

    case TOKEN_TERNARY:
    case TOKEN_ARRAY:
    case TOKEN_PAREN:
    case TOKEN_T_BRACE:
      skip_walk(sd, 0);
      stack_inc(sd, SSTACK__GROUP);
      return 0;

    case TOKEN_OP:
      

    case TOKEN_LIT:
      break;

    default:
      return 0;

  }


  int context = match_function(sd);
  if (context >= 0) {
    // we now expect "() {}"
    record_virt(sd, TOKEN_VALUE);
    stack_inc(sd, SSTACK__FUNC);
    sd->curr->context = context;
  }


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
    if (sd.tok.type == TOKEN_EOF && sd.curr == sd.stack) {
      break;  // process EOF until curr == stack
    }
  }

  if (sd.tok.type != TOKEN_EOF) {
    return ERROR__CLOSE;
  } else if (sd.curr != sd.stack) {
    printf("stack is %ld too high\n", sd.curr - sd.stack);
    return ERROR__STACK;
  }
  return ret;
}