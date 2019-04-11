
#include <string.h>
#include "simple.h"
#include "../utils.h"

#define SSTACK__BLOCK 1  // block execution context
#define SSTACK__GROUP 2  // group execution context "()" or "[]"
#define SSTACK__FUNC  3  // expects upcoming "() {}"
#define SSTACK__DICT  4  // within regular dict "{}"

// TODO: missing SSTACK__DO_WHILE ?


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


// stores a virtual token in the stream, and yields it before the current token
static void yield_virt(simpledef *sd, int type) {
  token *t = &(sd->curr->t1);
  sd->curr->t2 = *t;  // move t1 => t2
  bzero(t, sizeof(token));
  t->line_no = sd->curr->t2.line_no;  // ... on prev line
  t->type = type;

  sd->cb(sd->arg, t);
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


static int context_is_optional_keyword(uint8_t context, token *t) {
  if (t->type != TOKEN_LIT) {
    // do nothing
  } else if (context & CONTEXT__ASYNC && token_string(t, "await", 5)) {
    return 1;
  } else if (context & CONTEXT__GENERATOR && token_string(t, "yield", 5)) {
    return 1;
  }
  return 0;
}


static int context_is_label(uint8_t context, token *t) {
  return t->type == TOKEN_LIT &&
      !is_reserved_word(t->p, t->len, context & CONTEXT__STRICT) &&
      !context_is_optional_keyword(context, t);
}


static int context_is_unary(uint8_t context, token *t) {
  if (t->type != TOKEN_LIT) {
    return 0;
  }
  static const char always[] = " delete new typeof void ";
  return in_space_string(always, t->p, t->len) || context_is_optional_keyword(context, t);
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
    // FIXME: disallow keywords here (or just mark as TOKEN_KEYWORD)
    sd->tok.type = TOKEN_SYMBOL;
    skip_walk(sd, 0);  // consume name
  }

  return context;
}


// matches var/const/let, optional let based on context
static int match_decl(simpledef *sd) {
  if (!is_decl_keyword(sd->tok.p, sd->tok.len)) {
    return -1;
  }

  if (sd->tok.p[0] != 'l' || sd->next->type == TOKEN_BRACE || sd->next->type == TOKEN_ARRAY) {
    // OK: const, var or e.g. "let[..]" or "let{..}", destructuring
  } else if (sd->next->type != TOKEN_LIT || is_op_keyword(sd->next->p, sd->next->len)) {
    return -1;  // no following literal (e.g. "let = 1", "instanceof" counts as op)
  }

  sd->tok.type = TOKEN_KEYWORD;
  return record_walk(sd, 0);
}


// matches a "break foo;" or "continue;", emits ASI if required
static int match_label_keyword(simpledef *sd) {
  if (sd->tok.type != TOKEN_LIT || !is_label_keyword(sd->tok.p, sd->tok.len)) {
    return -1;
  }

  int line_no = sd->tok.line_no;

  sd->tok.type = TOKEN_KEYWORD;
  skip_walk(sd, 0);

  if (sd->tok.line_no == line_no && context_is_label(sd->curr->context, &(sd->tok))) {
    sd->tok.type = TOKEN_LABEL;
    skip_walk(sd, 0);
  }

  // e.g. "break\n" or "break foo\n"
  if (sd->tok.line_no != line_no) {
    yield_virt(sd, TOKEN_SEMICOLON);  // yield semi because line change
  } else if (sd->tok.type == TOKEN_SEMICOLON) {
    skip_walk(sd, 0);  // consume valid semi
  }

  return 0;
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


static void yield_valid_asi(simpledef *sd) {
  if ((!sd->curr->stype || sd->curr->stype == SSTACK__BLOCK) && sd->curr->t1.type) {
    // if parent is __BLOCK, just pretend a statement happened anyway
    yield_virt(sd, TOKEN_SEMICOLON);
  }
  // can't emit here (but JS probably invalid?)
}


static int simple_consume(simpledef *sd) {
restart:

  // dict state (left)
  if (sd->curr->stype == SSTACK__DICT) {
    uint8_t context = 0;

    // search for function
    // ... look for 'async' without '(' next
    if (sd->tok.type == TOKEN_LIT &&
        sd->td->next.type != TOKEN_PAREN &&
        token_string(&(sd->tok), "async", 5)) {
      sd->tok.type = TOKEN_KEYWORD;
      record_walk(sd, 0);
      context |= CONTEXT__ASYNC;
    }

    // ... look for '*'
    if (sd->tok.type == TOKEN_OP && token_string(&(sd->tok), "*", 1)) {
      context |= CONTEXT__GENERATOR;
      record_walk(sd, 0);
    }

    // ... look for get/set without '(' next
    if (sd->tok.type == TOKEN_LIT &&
        sd->td->next.type != TOKEN_PAREN &&
        is_getset(sd->tok.p, sd->tok.len)) {
      sd->tok.type = TOKEN_KEYWORD;
      record_walk(sd, 0);
    }

    // ... consumed all valid parts, this must be a symbol in dict
    if (sd->tok.type == TOKEN_LIT) {
      sd->tok.type = TOKEN_SYMBOL;
      record_walk(sd, 0);
    }

    switch (sd->tok.type) {
      case TOKEN_ARRAY:
        // e.g. "{[foo]: ...}"
        // FIXME: we're throwing away context and treating right-side as brand new
        record_walk(sd, 0);
        stack_inc(sd, SSTACK__GROUP);
        return 0;

      case TOKEN_PAREN:
        // ... don't need to consume
        stack_inc(sd, SSTACK__FUNC);
        sd->curr->context = context;
        return 0;

      case TOKEN_COMMA:  // valid
      default:           // invalid, but whatever
        return record_walk(sd, 0);

      case TOKEN_CLOSE:
        record_walk(sd, 0);
        --sd->curr;
        return 0;

      case TOKEN_COLON:
        record_walk(sd, 0);
        stack_inc(sd, SSTACK__GROUP);
        return 0;
    }
  }

  // function state, allow () or {}
  if (sd->curr->stype == SSTACK__FUNC) {
    if (sd->tok.type == TOKEN_PAREN) {
      // allow "function ()"
      record_walk(sd, 0);
      stack_inc(sd, SSTACK__GROUP);
      return 0;
    } else if (sd->tok.type == TOKEN_BRACE) {
      // terminal state of func, pop and insert normal block w/retained context
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

    // match anonymous block
    if (sd->tok.type == TOKEN_BRACE) {
      printf("got anon block\n");
      record_walk(sd, 0);
      stack_inc(sd, SSTACK__BLOCK);
      return 0;
    }

    // only care about lit here
    if (sd->tok.type != TOKEN_LIT) {
      goto regular_bail;
    }

    // match label
    if (context_is_label(sd->curr->context, &(sd->tok)) && sd->next->type == TOKEN_COLON) {
      sd->tok.type = TOKEN_LABEL;
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

    // match label keyword (e.g. "break foo;")
    if (match_label_keyword(sd) >= 0) {
      return 0;
    }

    // TODO: match hoisted class

    // match single-only
    if (token_string(&(sd->tok), "debugger", 8)) {
      int line_no = sd->tok.line_no;
      sd->tok.type = TOKEN_KEYWORD;
      record_walk(sd, 0);

      if (sd->tok.line_no != line_no) {
        yield_valid_asi(sd);
      }
      return 0;
    }

    // match restricted statement starters
    if (token_string(&(sd->tok), "return", 6) || token_string(&(sd->tok), "throw", 5)) {
      int line_no = sd->tok.line_no;
      sd->tok.type = TOKEN_KEYWORD;
      record_walk(sd, 0);

      if (sd->tok.line_no != line_no) {
        yield_valid_asi(sd);
        return 0;
      }

      goto regular_bail;  // e.g. "return ..." must be a statement
    }

    // match "import" which starts a statement
    // (we can refer back to pre-statement token to allow "from", "as" etc)
    if (token_string(&(sd->tok), "import", 6)) {
      sd->tok.type = TOKEN_KEYWORD;
      record_walk(sd, 0);
      goto regular_bail;  // starts statement
    }

    // match "export" which is sort of a no-op, resets to default state
    if (token_string(&(sd->tok), "export", 6)) {
      sd->tok.type = TOKEN_KEYWORD;
      record_walk(sd, 0);

      if (sd->tok.type == TOKEN_LIT && token_string(&(sd->tok), "default", 7)) {
        sd->tok.type = TOKEN_KEYWORD;
        record_walk(sd, 0);
      }

      // interestingly: "export default function() {}" is valid and a decl
      // so classic JS rules around decl must have names are ignored
      // ... "export default if (..)" is invalid, so we don't care if you do wrong things after
      return 0;
    }

    // match "var", "let" and "const"
    if (match_decl(sd) >= 0) {
      goto regular_bail;  // var, const, throw etc must create statement
    }

    // match e.g., "if", "catch"
    if (is_control_keyword(sd->tok.p, sd->tok.len)) {
      int maybe_paren = is_control_paren(sd->tok.p, sd->tok.len);
      sd->tok.type = TOKEN_KEYWORD;
      record_walk(sd, 0);

      // if we need a paren, consume without putting into statement
      if (maybe_paren && sd->tok.type == TOKEN_PAREN) {
        record_walk(sd, 0);
        stack_inc(sd, SSTACK__GROUP);
      }

      return 0;
    }

regular_bail:
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
      // relevant in "async () => blah, foo", reset from parent
      sd->curr->context = (sd->curr - 1)->context;
      return record_walk(sd, 0);

    case TOKEN_ARROW:
      if (!(sd->curr->t1.type == TOKEN_PAREN || sd->curr->t1.type == TOKEN_SYMBOL)) {
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
          printf("we now know that prior `async` is keyword\n");
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
      // are we on the right side of a dictionary?
      if (sd->curr->stype == SSTACK__GROUP &&
          (sd->curr - 1)->t1.type == TOKEN_COLON) {
        --sd->curr;
        if (sd->curr->stype != SSTACK__DICT) {
          return ERROR__INTERNAL;
        }
        goto restart;  // let dict handle this one (as if it was back on left)
      }

      if (!sd->curr->stype) {
        // closing a statement inside brace, yield ASI
        yield_valid_asi(sd);
        --sd->curr;  // finish pending value
      }

      // should be normal block or group, no other cases handled
      if (sd->curr->stype != SSTACK__BLOCK && sd->curr->stype != SSTACK__GROUP) {
        return ERROR__INTERNAL;
      }
      --sd->curr;

      // anything but ending up in naked block has value
      int has_value = (sd->curr->stype != SSTACK__BLOCK);
      if ((sd->curr + 1)->stype == SSTACK__GROUP && sd->curr->t1.type == TOKEN_TERNARY) {
        // ... but not "? ... :"
        has_value = 0;
      } else if (sd->curr->stype == SSTACK__DICT) {
        // ... but not in the left side of a dict
        has_value = 0;
      }

      if (sd->tok.type != TOKEN_EOF) {
        // noisy for EOF, where we don't care /shrug
        printf("popped stack (%ld) in op? has_value=%d (stype=%d)\n", sd->curr - sd->stack, has_value, sd->curr->stype);
      }
      return skip_walk(sd, has_value);

    case TOKEN_BRACE:
      if (sd->tok_has_value && !sd->curr->stype) {
        // found an invalid brace, restart as block
        if (sd->tok.line_no != sd->curr->t1.line_no) {
          yield_valid_asi(sd);
        }
        --sd->curr;
        goto restart;
      }
      record_walk(sd, 0);
      stack_inc(sd, SSTACK__DICT);
      return 0;

    case TOKEN_TERNARY:
    case TOKEN_ARRAY:
    case TOKEN_PAREN:
    case TOKEN_T_BRACE:
      record_walk(sd, 0);
      stack_inc(sd, SSTACK__GROUP);
      return 0;

    case TOKEN_LIT:
      if (!is_op_keyword(sd->tok.p, sd->tok.len)) {
        // nb. we catch "await", "delete", "new" etc below
        break;
      }
      sd->tok.type = TOKEN_OP;
      return record_walk(sd, 0);

    case TOKEN_DOT:
    case TOKEN_SPREAD:
    case TOKEN_OP: {
      int has_value = 0;
      if (sd->tok.type == TOKEN_OP && is_double_addsub(sd->tok.p, sd->tok.len)) {
        // if we had value, but are on new line, insert an ASI: this is a PostfixExpression that
        // disallows LineTerminator
        if (sd->tok_has_value && sd->tok.line_no != sd->curr->t1.line_no) {
          yield_valid_asi(sd);
          if (!sd->curr->stype) {
            --sd->curr;
          }
        } else {
          // ++ or -- don't change value-ness
          has_value = sd->tok_has_value;
        }
      }
      return record_walk(sd, has_value);
    }

    case TOKEN_STRING:
    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
      return record_walk(sd, 1);

    default:
      printf("unhandled token=%d `%.*s`\n", sd->tok.type, sd->tok.len, sd->tok.p);
      // fall-through

    case TOKEN_COLON:
      // always invalid here
      return record_walk(sd, 0);

  }

  int is_start = (sd->curr->t1.type == TOKEN_EOF);
  if (!is_start && sd->tok.line_no != sd->curr->t1.line_no && sd->tok_has_value) {
    sd->tok_has_value = 0;
    yield_valid_asi(sd);
    if (!sd->curr->stype) {
      --sd->curr;
    }
    goto restart;
  }

  int context = match_function(sd);
  if (context >= 0) {
    // non-hoisted function
    stack_inc(sd, SSTACK__FUNC);
    sd->curr->context = context;
    return 0;
  }

  // match valid unary ops
  if (context_is_unary(sd->curr->context, &(sd->tok))) {
    sd->tok.type = TOKEN_OP;
    record_walk(sd, 0);

    if (!sd->curr->stype && sd->curr->t1.p[0] == 'y' && sd->curr->t1.line_no != sd->tok.line_no) {
      // yield is a restricted keyword
      yield_valid_asi(sd);
      if (!sd->curr->stype) {
        --sd->curr;
      }
    }
    return 0;
  }

  // match curious case of decl inside "for ()"
  sstack *up = (sd->curr - 1);
  if (sd->curr->stype == SSTACK__GROUP &&
      sd->curr->t1.type == TOKEN_EOF &&
      up->t1.type == TOKEN_PAREN &&
      up->t2.type == TOKEN_KEYWORD &&
      token_string(&(up->t2), "for", 3)) {
    if (match_decl(sd) >= 0) {
      goto restart;
    }
  }

  // aggressive keyword match inside statement
  if (is_always_keyword(sd->tok.p, sd->tok.len, sd->curr->context & CONTEXT__STRICT)) {
    if (!sd->curr->stype && sd->tok.line_no != sd->curr->t1.line_no) {
      // if a keyword on a new line would make an invalid statement, restart with it
      yield_valid_asi(sd);
      --sd->curr;
      goto restart;
    }

    // ... otherwise, it's an invalid keyword
    // ... exception: `for (var x;;)` is valid
    sd->tok.type = TOKEN_KEYWORD;
    return record_walk(sd, 0);
  }

  // TODO(samthor): really fragile
  if (token_string(&(sd->tok), "as", 2)) {
    token *cand = &((sd->curr - 1)->t1);
    if (cand->type == TOKEN_KEYWORD && token_string(cand, "import", 6)) {
      sd->tok.type = TOKEN_KEYWORD;
      return record_walk(sd, 1);
    }
  }

  if (token_string(&(sd->tok), "async", 5)) {
    if (sd->next->type == TOKEN_LIT) {
      sd->tok.type = TOKEN_KEYWORD;
    } else if (sd->next->type == TOKEN_PAREN) {
      // otherwise, actually ambiguous: leave as TOKEN_LIT
      return record_walk(sd, 1);
    }
  }

  if (sd->tok.type == TOKEN_LIT) {
    sd->tok.type = TOKEN_SYMBOL;
  }
  return record_walk(sd, 1);
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

    if (ret) {
      // regular failure case
    } else if (prev == sd.tok.p) {
      printf("simple_consume didn't consume: %d %.*s\n", sd.tok.type, sd.tok.len, sd.tok.p);
      ret = ERROR__TODO;
    } else if (sd.tok.type == TOKEN_EOF) {
      ret = simple_consume(&sd);  // last run, run EOF to close statements
    } else {
      continue;
    }

    break;
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