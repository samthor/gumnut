
#include <string.h>
#include "parser.h"
#include "tokens/lit.h"

#define SSTACK__EXPR     0
#define SSTACK__CONTROL  1  // control group e.g. "for (...)"
#define SSTACK__BLOCK    2  // block execution context
#define SSTACK__DICT     3  // within regular dict "{}"
#define SSTACK__FUNC     4  // expects upcoming "name () {}"
#define SSTACK__CLASS    5  // expects "extends X"? "{}"
#define SSTACK__MODULE   6  // state machine for import/export defs
#define SSTACK__ASYNC    7  // async arrow function

#ifdef DEBUG
#include <stdio.h>
#define debugf(...) fprintf(stderr, __VA_ARGS__)
#else
#define debugf (void)sizeof
#endif


typedef struct {
  token prev;           // previous token
  uint32_t start;       // hash of stype start (set only for some stypes)
  uint8_t stype : 3;    // stack type
  uint8_t context : 3;  // current execution context (strict, async, generator)
} sstack;


typedef struct {
  tokendef *td;
  token *next;  // convenience
  token tok;
  int tok_has_value;  // has_value current tok was read with
  int is_module;

  prsr_callback cb;
  void *arg;
  int prev_line_no;

  sstack *curr;
  sstack stack[__STACK_SIZE];
} simpledef;


static sstack *stack_inc(simpledef *sd, uint8_t stype) {
  // TODO: check bounds
  ++sd->curr;
  bzero(sd->curr, sizeof(sstack));
  sd->curr->stype = stype;
  sd->curr->context = (sd->curr - 1)->context;  // copy context
  return sd->curr;
}


// stores a virtual token in the stream, and yields it before the current token
static void yield_virt(simpledef *sd, int type) {
  token *t = &(sd->curr->prev);
  bzero(t, sizeof(token));

  t->line_no = sd->prev_line_no;
  t->type = type;

  sd->cb(sd->arg, t);
}


static void yield_virt_skip(simpledef *sd, int type) {
  token t;
  bzero(&t, sizeof(token));

  t.line_no = sd->prev_line_no;
  t.type = type;

  sd->cb(sd->arg, &t);
}


// optionally yields ASI for restrict, assumes sd->curr->prev is the restricted keyword
// pops to nearby block
static int yield_restrict_asi(simpledef *sd) {
  int line_no = sd->curr->prev.line_no;

  if (line_no == sd->tok.line_no && sd->tok.type != TOKEN_CLOSE) {
    return 0;  // not new line, not close token
  }

  sstack *c = sd->curr;
  if (c->stype == SSTACK__BLOCK) {
    // ok
  } else if (c->stype == SSTACK__EXPR && (c - 1)->stype == SSTACK__BLOCK) {
    --sd->curr;
  } else {
    return 0;
  }

  yield_virt(sd, TOKEN_SEMICOLON);
  return 1;
}


// places the next useful token in sd->tok, yielding previous current
static int skip_walk(simpledef *sd, int has_value) {
  if (sd->tok.p) {
    sd->prev_line_no = sd->tok.line_no;
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
  sd->curr->prev = sd->tok;
  return skip_walk(sd, has_value);
}


static int is_optional_keyword(uint32_t hash, uint8_t context) {
  if (context & CONTEXT__ASYNC && hash == LIT_AWAIT) {
    return 1;
  } else if (context & (CONTEXT__GENERATOR | CONTEXT__STRICT) && hash == LIT_YIELD) {
    // yield is invalid outside a generator in strict mode, but it's a keyword
    return 1;
  }
  return 0;
}


static int is_always_keyword(uint32_t hash, uint8_t context) {
  return (hash & _MASK_KEYWORD) ||
      ((context & CONTEXT__STRICT) && (hash & _MASK_STRICT_KEYWORD));
}


static int is_label(token *t, uint8_t context) {
  if (t->type != TOKEN_LIT) {
    return 0;
  } else if (t->type == TOKEN_LABEL) {
    return 1;
  }
  return !is_always_keyword(t->hash, context) && !is_optional_keyword(t->hash, context);
}


static int is_valid_name(uint32_t hash, uint8_t context) {
  uint32_t mask = _MASK_KEYWORD | _MASK_MASQUERADE;
  if (context & CONTEXT__STRICT) {
    mask |= _MASK_STRICT_KEYWORD;
  }

  if ((context & CONTEXT__ASYNC) && hash == LIT_AWAIT) {
    // await is a keyword inside async function
    return 0;
  }

  if ((context & CONTEXT__GENERATOR) && hash == LIT_YIELD) {
    // yield is a keyword inside generator function
    return 0;
  }

  return !(hash & mask);
}


static int is_unary(uint32_t hash, uint8_t context) {
  // check if we're also a keyword, to avoid matching 'await' and 'yield' by default
  uint32_t mask = _MASK_UNARY_OP | _MASK_KEYWORD;
  return ((hash & mask) == mask) || is_optional_keyword(hash, context);
}


// matches any current function decl/stmt
static int match_function(simpledef *sd) {
  if (sd->tok.hash == LIT_ASYNC) {
    if (sd->next->hash != LIT_FUNCTION) {
      return -1;
    }
    // otherwise fine
  } else if (sd->tok.hash != LIT_FUNCTION) {
    return -1;
  }

  uint8_t context = (sd->curr->context & CONTEXT__STRICT);
  if (sd->tok.hash == LIT_ASYNC) {
    context |= CONTEXT__ASYNC;
    sd->tok.type = TOKEN_KEYWORD;
    skip_walk(sd, -1);  // consume "async"
  }
  sd->tok.type = TOKEN_KEYWORD;
  record_walk(sd, 0);  // consume "function"

  // optionally consume generator star
  if (sd->tok.hash == MISC_STAR) {
    skip_walk(sd, 0);
    context |= CONTEXT__GENERATOR;
  }

  // nb. does NOT consume name
  return context;
}


static int match_class(simpledef *sd) {
  if (sd->tok.hash != LIT_CLASS) {
    return -1;
  }
  sd->tok.type = TOKEN_KEYWORD;
  record_walk(sd, 0);  // consume "class"

  // optionally consume class name if not "extends"
  uint32_t h = sd->tok.hash;
  if (h == LIT_EXTENDS || sd->tok.type != TOKEN_LIT) {
    return 0;  // ... if this isn't TOKEN_BRACE, it's invalid, but let stack handler deal
  } else if (!is_valid_name(h, sd->curr->context) || h == LIT_YIELD || h == LIT_LET) {
    // nb. "yield" or "let" are both always invalid, even in non-strict (doesn't apply to function)
    // ... this might actually be a V8 "feature", but it's the same in Firefox, and both actually
    // complain that it's invalid in strict mode even if _not_ in that mode (!)
    sd->tok.type = TOKEN_KEYWORD;  // "class if" is invalid
  } else {
    sd->tok.type = TOKEN_SYMBOL;
  }
  skip_walk(sd, 0);  // consume name even if it's an invalid keyword
  return 0;
}


static int enact_defn(simpledef *sd) {
  // ...match function
  int context = match_function(sd);
  if (context >= 0) {
    stack_inc(sd, SSTACK__FUNC);
    sd->curr->context = context;
    return 1;
  }

  // ... match class
  int class = match_class(sd);
  if (class >= 0) {
    stack_inc(sd, SSTACK__CLASS);
    return 1;
  }

  return 0;
}


// matches a "break foo;" or "continue;", emits ASI if required
static int match_label_keyword(simpledef *sd) {
  if (sd->tok.hash != LIT_BREAK && sd->tok.hash != LIT_CONTINUE) {
    return -1;
  }

  int line_no = sd->tok.line_no;
  sd->tok.type = TOKEN_KEYWORD;
  record_walk(sd, 0);

  if (sd->tok.line_no == line_no && is_label(&(sd->tok), sd->curr->context)) {
    sd->tok.type = TOKEN_LABEL;
    skip_walk(sd, 0);  // don't consume, so yield_restrict_asi works
  }

  // e.g. "break\n" or "break foo\n"
  if (!yield_restrict_asi(sd) && sd->tok.type == TOKEN_SEMICOLON) {
    skip_walk(sd, -1);  // emit or consume valid semicolon
  }
  return 0;
}


static int is_use_strict(token *t) {
  if (t->type != TOKEN_STRING || t->len != 12) {
    return 0;
  }
  return !memcmp(t->p, "'use strict'", 12) || !memcmp(t->p, "\"use strict\"", 12);
}


// is the next token valuelike for a previous valuelike?
// used directly only for "let" and "await" (at top-level), so doesn't include e.g. paren or array,
// as these would be indexing or calling
static int is_token_valuelike(token *t) {
  switch (t->type) {
    case TOKEN_LIT:
      // _any_ lit is fine (even keywords, even if invalid) except "in" and "instanceof"
      return !(t->hash & _MASK_REL_OP);

    case TOKEN_SYMBOL:
    case TOKEN_NUMBER:
    case TOKEN_STRING:
    case TOKEN_BRACE:
      return 1;

    case TOKEN_OP:
      // https://www.ecma-international.org/ecma-262/9.0/index.html#prod-UnaryExpression
      // FIXME: in Chrome's top-level await support (e.g. in DevTools), this also includes + and -
      return t->hash == MISC_NOT || t->hash == MISC_BITNOT;
  }

  return 0;
}


// is this token valuelike following "of" inside a "for (... of ...)"?
static int is_token_valuelike_keyword(token *t) {
  if (is_token_valuelike(t)) {
    return 1;
  }
  switch (t->type) {
    case TOKEN_PAREN:
    case TOKEN_ARRAY:
    case TOKEN_BRACE:
    case TOKEN_SLASH:
      return 1;
  }
  return 0;
}


// matches var/const/let, optional let based on context
static int match_decl(simpledef *sd) {
  if (!(sd->tok.hash & _MASK_DECL)) {
    return -1;
  }

  // in strict mode, 'let' is always a keyword (well, reserved)
  if (!(sd->curr->context & CONTEXT__STRICT) && sd->tok.hash == LIT_LET) {
    if (!is_token_valuelike(sd->next) && sd->next->type != TOKEN_ARRAY) {
      // ... let[] is declaration, but e.g. "await []" is an index
      return -1;
    }
    // OK: destructuring "let[..]" or "let{..}", and not with "in" or "instanceof" following
  }

  sd->tok.type = TOKEN_KEYWORD;
  return record_walk(sd, 0);
}


// can the current token create a trailing control group from prev
static inline int may_trail_control(uint32_t prev, uint32_t curr) {
  if (curr & _MASK_CONTROL) {
    switch (prev) {
      case LIT_IF:
        return curr == LIT_ELSE;

      case LIT_DO:
        debugf("may_trail_control called with LIT_DO\n");
        return curr == LIT_WHILE;

      case LIT_TRY:
        return curr == LIT_CATCH || curr == LIT_FINALLY;

      case LIT_CATCH:
        return curr == LIT_FINALLY;
    }
  }
  return 0;
}


static int simple_start_arrowfunc(simpledef *sd, int async) {
#ifdef DEBUG
  if (sd->tok.type != TOKEN_ARROW) {
    debugf("arrowfunc start without TOKEN_ARROW\n");
    return ERROR__ASSERT;
  }
  if (sd->curr->stype != SSTACK__EXPR) {
    debugf("arrowfunc start not inside EXPR\n");
    return ERROR__ASSERT;
  }
#endif

  uint8_t context = (sd->curr->context & CONTEXT__STRICT);
  if (async) {
    context |= CONTEXT__ASYNC;
  }

  if (sd->next->type == TOKEN_BRACE) {
    // the sensible arrow function case, with a proper body
    // e.g. "() => { statements }"
    record_walk(sd, -1);  // consume =>
    sd->tok.type = TOKEN_EXEC;
    record_walk(sd, -1);  // consume {
    stack_inc(sd, SSTACK__BLOCK);
    sd->curr->prev.type = TOKEN_TOP;
  } else {
    // just change statement's context (e.g. () => async () => () => ...)
    record_walk(sd, -1);  // consume =>
    sd->curr->prev.type = TOKEN_EOF;  // pretend statement finished
  }
  sd->curr->context = context;
  return 0;
}


// consumes an expr (always SSTACK__EXPR)
// MUST NOT assume parent is SSTACK__BLOCK, could be anything
static int simple_consume_expr(simpledef *sd) {
  switch (sd->tok.type) {
    case TOKEN_SEMICOLON:
      if ((sd->curr - 1)->stype == SSTACK__BLOCK) {
#ifdef DEBUG
        if (sd->curr->start) {
          debugf("block expr must not have real start token\n");
          return ERROR__ASSERT;
        }
#endif
        --sd->curr;
      }
      record_walk(sd, -1);  // semi goes in block
      return 0;

    case TOKEN_COMMA:
      // restart expr
      --sd->curr;
      if (sd->curr->stype == SSTACK__DICT) {
        // ... unless it's a dict which puts us back on left
        return 0;
      }
      stack_inc(sd, SSTACK__EXPR);
      return skip_walk(sd, -1);

    case TOKEN_ARROW:
      if (!(sd->curr->prev.type == TOKEN_PAREN || sd->curr->prev.type == TOKEN_SYMBOL)) {
        // not a valid arrow func, treat as op
        return record_walk(sd, -1);
      }
      return simple_start_arrowfunc(sd, 0);

    case TOKEN_CLOSE: {
      sstack *prev = sd->curr;
      --sd->curr;  // always valid to close here (SSTACK__BLOCK catches invalid close)

      switch (sd->curr->stype) {
        case SSTACK__BLOCK: {
          // parent is block, maybe yield ASI but pop either way
          if (prev->prev.type) {
            yield_virt(sd, TOKEN_SEMICOLON);
          }
          return 0;
        }

        case SSTACK__ASYNC:
          // we're in "async ()", expect arrow next, but if not, we have value
          skip_walk(sd, 1);
          return 0;

        default:
          // this would be hoisted class/func or control group, not a value after
          if (prev->start) {
            // ... had a start token, so walk over close token
            skip_walk(sd, 0);
          } else {
            debugf("handing close to parent stype\n");
            // ... got a close while in expr which isn't in group, let parent handle
            // probably error, e.g. "{ class extends }"
          }
          return 0;

        case SSTACK__EXPR:
          break;
      }

      // value if this places us into a statement/group (but not if this was ternary)
      int has_value = (sd->curr->stype == SSTACK__EXPR && sd->curr->prev.type != TOKEN_TERNARY);
      skip_walk(sd, has_value);
      return 0;
    }

    case TOKEN_BRACE:
      if (sd->tok_has_value && !sd->curr->start) {
        // found an invalid brace, restart as block
        int yield = (sd->tok.line_no != sd->curr->prev.line_no &&
            sd->curr->prev.type &&
            (sd->curr - 1)->stype == SSTACK__BLOCK);
        --sd->curr;
        if (yield) {
          yield_virt(sd, TOKEN_SEMICOLON);
        }
        return 0;
      }
      sd->tok.type = TOKEN_DICT;
      record_walk(sd, -1);
      stack_inc(sd, SSTACK__DICT);
      return 0;

    case TOKEN_TERNARY:
    case TOKEN_ARRAY:
    case TOKEN_PAREN:
    case TOKEN_T_BRACE:
      record_walk(sd, -1);
      stack_inc(sd, SSTACK__EXPR);
      sd->curr->start = sd->tok.type;
      return 0;

    case TOKEN_LIT:
      if (sd->tok.hash & _MASK_REL_OP) {
        sd->tok.type = TOKEN_OP;
        return record_walk(sd, 0);
      }
      // nb. we catch "await", "delete", "new" etc below
      // fall-through

    case TOKEN_STRING:
      if (sd->curr->prev.type == TOKEN_T_BRACE) {
        // if we're a string following ${}, this is part a of a template literal and doesn't have
        // special ASI casing (e.g. '${\n\n}' isn't really causing a newline)
        return record_walk(sd, -1);
      }
      // fall-through

    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
      // basic ASI detection inside statement: value on a new line than before, with value
      if ((sd->curr - 1)->stype == SSTACK__BLOCK &&
          sd->tok.line_no != sd->curr->prev.line_no &&
          sd->tok_has_value &&
          sd->curr->prev.type) {
        sd->tok_has_value = 0;
        --sd->curr;
        yield_virt(sd, TOKEN_SEMICOLON);
        return 0;
      }

      if (sd->tok.type == TOKEN_LIT) {
        if (sd->tok.hash) {
          break;  // special lit handling
        }
        // ... no hash, always just a regular value
        sd->tok.type = TOKEN_SYMBOL;
      }
      return record_walk(sd, 1);  // otherwise, just a regular value

    case TOKEN_OP: {
      int has_value = 0;
      if (sd->tok.hash == MISC_INCDEC) {
        // if we had value, but are on new line, insert an ASI: this is a PostfixExpression that
        // disallows LineTerminator
        if ((sd->curr - 1)->stype == SSTACK__BLOCK &&
            sd->tok_has_value &&
            sd->tok.line_no != sd->curr->prev.line_no) {
          // nb. if we're not inside SSTACK__BLOCK, line changes here are invalid
          sd->tok_has_value = 0;
          int yield = (sd->curr->prev.type);
          --sd->curr;
          if (yield) {
            yield_virt(sd, TOKEN_SEMICOLON);
          }
          return 0;
        }

        // ++ or -- don't change value-ness
        has_value = sd->tok_has_value;
      }
      return record_walk(sd, has_value);
    }

    case TOKEN_COLON:
      if ((sd->curr - 1)->stype == SSTACK__BLOCK) {
        --sd->curr;  // this catches cases like "case {}:", pretend that was an expr on its own
      } else {
        // does nothing here (invalid)
      }
      return record_walk(sd, -1);

    default:
      // nb. This is likely because we haven't resolved a TOKEN_SLASH somewhere.
      debugf("unhandled token=%d `%.*s`\n", sd->tok.type, sd->tok.len, sd->tok.p);
      return ERROR__INTERNAL;

  }

  // match function or class as value
  if (enact_defn(sd)) {
    return 0;
  }

  uint32_t outer_hash = sd->tok.hash;

  // match valid unary ops
  if (is_unary(outer_hash, sd->curr->context)) {
    sd->tok.type = TOKEN_OP;
    record_walk(sd, 0);

    if (sd->curr->prev.hash == LIT_YIELD) {
      // yield is a restricted keyword (this does nothing inside group, but is invalid)
      yield_restrict_asi(sd);
    }
    return 0;
  }

  // match non-async await: this is valid iff it _looks_ like unary op use (e.g. await value).
  // this is a lookahead for value, rather than what we normally do
  if (outer_hash == LIT_AWAIT && is_token_valuelike(sd->next)) {
    // ... to be clear, this is an error, but it IS parsed as a keyword
    sd->tok.type = TOKEN_KEYWORD;
    return record_walk(sd, 0);
  }

  // match curious cases inside "for ("
  sstack *up = (sd->curr - 1);
  if (up->stype == SSTACK__CONTROL && up->start == LIT_FOR && sd->curr->stype == SSTACK__EXPR) {

    // start of "for (", look for decl (var/let/const) and mark as keyword
    if (!sd->curr->prev.type) {
      if (match_decl(sd) >= 0) {
        return 0;
      }
    }

    // find "of" between two value-like things
    if (outer_hash == LIT_OF &&
        sd->tok_has_value &&
        is_token_valuelike_keyword(sd->next)) {
      sd->tok.type = TOKEN_OP;
      return record_walk(sd, 0);
    }
  }

  // aggressive keyword match inside statement
  if (is_always_keyword(outer_hash, sd->curr->context)) {
    if (up->stype == SSTACK__BLOCK && sd->curr->prev.type && sd->tok.line_no != sd->curr->prev.line_no) {
      // if a keyword on a new line would make an invalid statement, restart with it
      --sd->curr;
      yield_virt(sd, TOKEN_SEMICOLON);
      return 0;
    }
    // ... otherwise, it's an invalid keyword
    sd->tok.type = TOKEN_KEYWORD;
    return record_walk(sd, 0);
  }

  // look for async arrow function
  if (outer_hash == LIT_ASYNC) {
    switch (sd->curr->prev.type) {
      case TOKEN_OP:
        if (sd->curr->prev.hash != MISC_EQUALS) {
          // ... "1 + async () => x" is invalid, only "... = async () =>" is fine
          break;
        }
        // fall-through

      case TOKEN_EOF:
        switch (sd->next->type) {
          case TOKEN_LIT:
            sd->tok.type = TOKEN_KEYWORD;  // "async foo" always makes keyword
            // fall-through

          case TOKEN_PAREN:
            // consume and push SSTACK__ASYNC even if we already know keyword
            // ... otherwise this explicitly remains as LIT until resolved
            record_walk(sd, -1);
            stack_inc(sd, SSTACK__ASYNC);
            return 0;
        }
    }

    sd->tok.type = TOKEN_SYMBOL;
    return record_walk(sd, 1);
  }

  // if nothing else known, treat as symbol
  if (sd->tok.type == TOKEN_LIT) {
    sd->tok.type = TOKEN_SYMBOL;
  }
  return record_walk(sd, 1);
}


// must be called in SSTACK__BLOCK state
static int maybe_close_control(simpledef *sd, token *t) {
  sstack *c = sd->curr;
#ifdef DEBUG
  if (c->stype != SSTACK__BLOCK) {
    debugf("got maybe_close_control outside SSTACK__BLOCK\n");
    return ERROR__ASSERT;
  }
#endif

  if (c == sd->stack) {
    return 0;  // can't close top or even check above
  } else if (t && t->type == TOKEN_CLOSE) {
    // allowed, this might be e.g. "if { if 1 }"
  } else if (!c->prev.type || c->prev.type == TOKEN_COLON) {
    // ... special-cases TOKEN_COLON to allow labels
    return 0;
  }

  if ((c - 1)->prev.p) {
    // if parent is non-NULL, then wait for the closing }
    debugf("but doing nothing\n");
    return 0;
  }
  // ... otherwise, we have a virtual TOKEN_EXEC!

  --sd->curr;
#ifdef DEBUG
  if (sd->curr->stype != SSTACK__CONTROL || sd->curr->prev.type != TOKEN_EXEC) {
    return ERROR__ASSERT;
  }
#endif
  return 1;
}


static int simple_consume(simpledef *sd) {
  switch (sd->curr->stype) {
    // async arrow function state
    case SSTACK__ASYNC:
      switch (sd->curr->prev.type) {
        default:
          debugf("invalid type in SSTACK__ASYNC: %d\n", sd->curr->prev.type);
          --sd->curr;
          return 0;

        case TOKEN_EOF:
          // start of ambig, insert expr
          if (sd->tok.type == TOKEN_PAREN) {
            record_walk(sd, -1);
            stack_inc(sd, SSTACK__EXPR);
            sd->curr->start = TOKEN_PAREN;
            return 0;
          } else if (sd->tok.type != TOKEN_LIT) {
            return ERROR__INTERNAL;
          }

          // set type of 'x' in "async x =>": keywords are invalid, but allow anyway
          sd->tok.type = is_always_keyword(sd->tok.hash, sd->curr->context) ? TOKEN_KEYWORD : TOKEN_SYMBOL;
          record_walk(sd, 0);
          break;

        case TOKEN_PAREN: {
          // end of ambig, check whether arrow exists
          token *yield = &((sd->curr - 1)->prev);
          yield->type = (sd->tok.type == TOKEN_ARROW ? TOKEN_KEYWORD : TOKEN_SYMBOL);
          yield->mark = MARK_RESOLVE;
          sd->cb(sd->arg, yield);
          break;
        }
      }

      if (sd->tok.type != TOKEN_ARROW) {
        debugf("async starter without arrow, ignoring (%d)\n", sd->tok.type);
        --sd->curr;
        return 0;
      }

      --sd->curr;  // pop SSTACK__ASYNC
      return simple_start_arrowfunc(sd, 1);

    // import state
    case SSTACK__MODULE:
      switch (sd->tok.type) {
        case TOKEN_BRACE:
          sd->tok.type = TOKEN_DICT;
          record_walk(sd, -1);
          stack_inc(sd, SSTACK__MODULE);
          return 0;

        // unexpected, but handle anyway
        case TOKEN_T_BRACE:
        case TOKEN_PAREN:
        case TOKEN_ARRAY:
          record_walk(sd, -1);
          stack_inc(sd, SSTACK__EXPR);
          sd->curr->start = sd->tok.type;
          return 0;

        case TOKEN_LIT:
          break;

        case TOKEN_COMMA:
          return record_walk(sd, -1);

        case TOKEN_CLOSE:
          if ((sd->curr - 1)->stype != SSTACK__MODULE) {
            debugf("module internal error\n");
            return ERROR__INTERNAL;  // impossible, we're at top-level
          }
          int line_no = sd->tok.line_no;
          skip_walk(sd, 0);
          --sd->curr;  // close inner

          if ((sd->curr - 1)->stype == SSTACK__MODULE) {
            return 0;  // invalid several descendant case
          }
          --sd->curr;  // close outer

          if (sd->tok.hash == LIT_FROM) {
            // ... inner {} must have trailer "from './path'"
            sd->tok.type = TOKEN_KEYWORD;
            record_walk(sd, 0);
            if (sd->tok.type == TOKEN_STRING) {
              sd->tok.mark = MARK_IMPORT;
            }
          } else if (sd->tok.type != TOKEN_SEMICOLON && sd->tok.line_no != line_no) {
            // ... or just abandon, generating semi if needed (valid in export case)
            yield_virt(sd, TOKEN_SEMICOLON);
          }
          return 0;

        case TOKEN_OP:
          if (sd->tok.hash == MISC_STAR) {
            sd->tok.type = TOKEN_SYMBOL;  // pretend this is symbol
            return record_walk(sd, -1);
          }
          // fall-through

        default:
          if ((sd->curr - 1)->stype != SSTACK__MODULE) {
            debugf("abandoning module for reasons: %d\n", sd->tok.type);
            --sd->curr;
            return 0;  // not inside submodule, just give up
          }
          return record_walk(sd, 0);
      }

      // consume and bail out on "from" if it follows a symbol or close brace
      if ((sd->curr - 1)->stype != SSTACK__MODULE &&
          sd->curr->prev.type == TOKEN_SYMBOL &&
          sd->tok.hash == LIT_FROM) {
        --sd->curr;  // close outer
        sd->tok.type = TOKEN_KEYWORD;
        record_walk(sd, 0);

        // restart as regular statement, marking as import name
        if (sd->tok.type == TOKEN_STRING) {
          sd->tok.mark = MARK_IMPORT;
        }
        return 0;
      }

      // consume "as" as a keyword if it follows a symbol
      if (sd->curr->prev.type == TOKEN_SYMBOL && sd->tok.hash == LIT_AS) {
        sd->tok.type = TOKEN_KEYWORD;
        return record_walk(sd, 0);
      }

      // otherwise just mask as symbol or keyword
      if (is_valid_name(sd->tok.hash, sd->curr->context)) {
        sd->tok.type = TOKEN_SYMBOL;
      } else {
        // ... invalid, of course
        sd->tok.type = TOKEN_KEYWORD;
      }
      return record_walk(sd, 0);

    // dict state (left)
    case SSTACK__DICT: {
      uint8_t context = 0;

      // search for function
      // ... look for 'static' without '(' next
      if (sd->td->next.type != TOKEN_PAREN &&
          sd->tok.hash == LIT_STATIC) {
        sd->tok.type = TOKEN_KEYWORD;
        record_walk(sd, 0);
      }

      // ... look for 'async' without '(' next
      if (sd->td->next.type != TOKEN_PAREN &&
          sd->tok.hash == LIT_ASYNC) {
        sd->tok.type = TOKEN_KEYWORD;
        record_walk(sd, 0);
        context |= CONTEXT__ASYNC;
      }

      // ... look for '*'
      if (sd->tok.hash == MISC_STAR) {
        context |= CONTEXT__GENERATOR;
        record_walk(sd, -1);
      }

      // ... look for get/set without '(' next
      if (sd->td->next.type != TOKEN_PAREN &&
          (sd->tok.hash == LIT_GET || sd->tok.hash == LIT_SET)) {
        sd->tok.type = TOKEN_KEYWORD;
        record_walk(sd, 0);
      }

      // terminal state of left side
      switch (sd->tok.type) {
        // ... anything that looks like it could be a function, that way (and let stack fail)
        case TOKEN_STRING:
          if (sd->tok.p[0] == '`' || sd->next->type != TOKEN_PAREN) {
            break;  // don't allow anything but " 'foo' ( "
          }
          // fall-through

        case TOKEN_LIT:
        case TOKEN_PAREN:
        case TOKEN_BRACE:
        case TOKEN_ARRAY:
          debugf("pretending to be function: %.*s\n", sd->tok.len, sd->tok.p);
          stack_inc(sd, SSTACK__FUNC);
          sd->curr->context = context;
          return 0;

        case TOKEN_COLON:
          record_walk(sd, -1);
          stack_inc(sd, SSTACK__EXPR);
          debugf("pushing stmt for colon\n");
          return 0;

        case TOKEN_CLOSE:
          --sd->curr;
          debugf("closing dict, value=%d level=%ld\n", sd->curr->stype == SSTACK__EXPR, sd->curr - sd->stack);
          skip_walk(sd, (sd->curr->stype == SSTACK__EXPR));
          return 0;

        case TOKEN_COMMA:  // valid
          return record_walk(sd, -1);
      }

      // if this a single literal, it's valid: e.g. {'abc':def}
      // ... but we pretend it's an expression anyway (and : closes it)
      debugf("starting expr inside left dict\n");
      stack_inc(sd, SSTACK__EXPR);
      return 0;
    }

    // function state, allow () or {}
    case SSTACK__FUNC:
      switch (sd->tok.type) {
        case TOKEN_ARRAY:
          // allow "function ['name']" (for dict)
          record_walk(sd, -1);
          stack_inc(sd, SSTACK__EXPR);
          sd->curr->start = TOKEN_ARRAY;

          // ... but "{async [await 'name']..." doesn't take await from our context
          sd->curr->context = (sd->curr - 2)->context;
          return 0;

        case TOKEN_STRING:
          // allow "function 'foo'" (for dict)
          if (sd->tok.p[0] == '`') {
            break;  // don't allow template literals
          }
          return record_walk(sd, -1);

        case TOKEN_LIT: {
          sstack *p = (sd->curr - 1);  // use context from parent, "async function await() {}" is valid :(

          // we're only maybe a keyword in non-dict modes
          if (p->stype != SSTACK__DICT && !is_valid_name(sd->tok.hash, p->context)) {
            sd->tok.type = TOKEN_KEYWORD;
          } else {
            sd->tok.type = TOKEN_SYMBOL;
          }
          return record_walk(sd, 0);
        }

        case TOKEN_PAREN:
          // allow "function ()"
          record_walk(sd, -1);
          stack_inc(sd, SSTACK__EXPR);
          sd->curr->start = TOKEN_PAREN;
          return 0;

        case TOKEN_BRACE: {
          // terminal state of func, pop and insert normal block w/retained context
          uint8_t context = sd->curr->context;
          --sd->curr;
          sd->tok.type = TOKEN_EXEC;
          record_walk(sd, -1);
          stack_inc(sd, SSTACK__BLOCK);
          sd->curr->prev.type = TOKEN_TOP;
          sd->curr->context = context;
          return 0;
        }
      }

      // invalid, abandon function def
      debugf("invalid function construct\n");
      --sd->curr;
      return 0;

    // class state, just insert group (for extends) or dict-like
    case SSTACK__CLASS: {
      if (!sd->curr->prev.type && sd->tok.hash == LIT_EXTENDS) {
        // ... check for extends, valid
        sd->tok.type = TOKEN_KEYWORD;
        record_walk(sd, 0);  // consume "extends" keyword, treat as non-value
        stack_inc(sd, SSTACK__EXPR);
        return 0;
      }

      if (sd->tok.type == TOKEN_BRACE) {
        // start dict-like block (pop SSTACK__CLASS)
        --sd->curr;
        sd->tok.type = TOKEN_DICT;
        record_walk(sd, -1);
        stack_inc(sd, SSTACK__DICT);
        return 0;
      }

      // invalid, abandon class def
      debugf("invalid class construct\n");
      --sd->curr;
      return 0;
    }

    // control group state
    case SSTACK__CONTROL:
      // if we had an exec, then abandon: all done
      if (sd->curr->prev.type == TOKEN_EXEC) {
        if (!sd->curr->prev.p) {
restart_control:
          // ... was virtual exec, emit close (real token didn't close us)
          yield_virt_skip(sd, TOKEN_CLOSE);
        }

        if (sd->curr->start == LIT_DO) {
          // ... search for trailer "while ("
          if (sd->next->type == TOKEN_PAREN && sd->tok.hash == LIT_WHILE) {
            sd->tok.type = TOKEN_KEYWORD;
            record_walk(sd, -1);  // consume while
            record_walk(sd, -1);  // consume paren
            stack_inc(sd, SSTACK__EXPR);
            sd->curr->start = TOKEN_PAREN;
            return 0;
          }
          debugf("invalid do-while, abandoning\n");
        } else if (may_trail_control(sd->curr->start, sd->tok.hash)) {
          debugf("control closed but found trailer: %.*s\n", sd->tok.len, sd->tok.p);
          --sd->curr;  // leave SSTACK__CONTROL but _not_ the parent SSTACK__BLOCK
          return 0;
        }

check_single_block:
        --sd->curr;  // leave SSTACK__CONTROL
#ifdef DEBUG
        if (sd->curr->stype != SSTACK__BLOCK) {
          debugf("control found NOT in block\n");
          return ERROR__ASSERT;
        }
#endif

        // repeat if within a single block (NULL pointer)
        if (sd->curr != sd->stack && !(sd->curr - 1)->prev.p) {
          debugf("closed control inside ANOTHER control\n");
          --sd->curr;
          goto restart_control;
        }
        return 0;
      }

      // look for close of do-while ()'s
      if (sd->curr->start == LIT_DO && sd->curr->prev.type == TOKEN_PAREN) {
        debugf("matching do-while paren end\n");
        // this is end of valid group, emit ASI if there's not one
        // occurs regardless of newline, e.g. "do;while(0)foo" is valid, ASI after close paren
        if (sd->tok.type == TOKEN_SEMICOLON) {
          skip_walk(sd, -1);
        } else {
          yield_virt(sd, TOKEN_SEMICOLON);
        }
        // FIXME: gross, but we want to check if this semi closes anything else
        goto check_single_block;
      }

#ifdef DEBUG
      if (sd->curr->prev.type && sd->curr->prev.type != TOKEN_PAREN && !(sd->curr->prev.hash & _MASK_CONTROL)) {
        debugf("control exec must only start after blank, paren or control\n");
        return ERROR__ASSERT;
      }
#endif

      // otherwise, start an exec block!
      if (sd->tok.type == TOKEN_BRACE) {
        // ... found e.g., "if {}"
        sd->tok.type = TOKEN_EXEC;
        record_walk(sd, -1);
        stack_inc(sd, SSTACK__BLOCK);
      } else {
        // ... found e.g. "if something_else", push virtual exec block
        yield_virt(sd, TOKEN_EXEC);
        stack_inc(sd, SSTACK__BLOCK);
      }
      return 0;

    case SSTACK__EXPR:
      return simple_consume_expr(sd);

    default:
      debugf("unhandled stype=%d\n", sd->curr->stype);
      return ERROR__INTERNAL;

    // zero state, determine what to push
    case SSTACK__BLOCK:
      break;
  }

  if (maybe_close_control(sd, &sd->tok)) {
    // FIXME: we could call this in outer method
    // yield back to SSTACK__CONTROL
    return 0;
  }

  switch (sd->tok.type) {
    case TOKEN_BRACE:
      // anon block
      debugf("unattached exec block\n");
      sd->tok.type = TOKEN_EXEC;
      record_walk(sd, -1);
      stack_inc(sd, SSTACK__BLOCK);
      return 0;

    case TOKEN_CLOSE:
      if (sd->curr == sd->stack) {
        // ... top-level, invalid CLOSE
        debugf("invalid close\n");
      } else {
       --sd->curr;  // pop out of block or dict
 #ifdef DEBUG
        if (sd->curr->stype == SSTACK__CONTROL && sd->curr->prev.type == TOKEN_EXEC && !sd->curr->prev.p) {
          debugf("TOKEN_CLOSE inside SSTACK__BLOCK for single block\n");
          return ERROR__ASSERT;
        }
#endif
      }

      // "function {}" that ends in statement/group has value
      skip_walk(sd, sd->curr->stype == SSTACK__EXPR);
      return 0;

    case TOKEN_LIT:
      break;  // only care about lit below

    case TOKEN_STRING:
      // match 'use strict'
      do {
        if (sd->curr->prev.type != TOKEN_TOP) {
          break;
        }

        // FIXME: gross, lookahead to confirm that 'use strict' is on its own or generate ASI
        if (sd->next->type == TOKEN_SEMICOLON) {
          // great
        } else {
          if (sd->next->line_no == sd->tok.line_no) {
            // ... can't generate ASI
            break;
          }
          if (sd->next->hash & _MASK_REL_OP) {
            // binary oplike cases ('in', 'instanceof')
            break;
          }
          if (sd->next->type == TOKEN_OP) {
            if (sd->next->hash != MISC_INCDEC) {
              // ... ++/-- causes ASI
              break;
            }
          } else if (!is_token_valuelike(sd->next)) {
            break;
          }
        }

        if (is_use_strict(&(sd->tok))) {
          debugf("setting 'use strict'\n");
          sd->curr->context |= CONTEXT__STRICT;
        }
      } while (0);

    default:
      goto block_bail;  // non-lit starts statement
  }

  // match label
  if (is_label(&(sd->tok), sd->curr->context) && sd->next->type == TOKEN_COLON) {
    sd->tok.type = TOKEN_LABEL;
    skip_walk(sd, -1);    // consume label
    record_walk(sd, -1);  // consume colon and record (to prevent bad 'use strict')
    return 0;
  }

  // match label keyword (e.g. "break foo;")
  if (match_label_keyword(sd) >= 0) {
    return 0;
  }

  uint32_t outer_hash = sd->tok.hash;

  // match single-only
  if (outer_hash == LIT_DEBUGGER) {
    sd->tok.type = TOKEN_KEYWORD;
    record_walk(sd, 0);
    yield_restrict_asi(sd);
    return 0;
  }

  // match restricted statement starters
  if (outer_hash == LIT_RETURN || outer_hash == LIT_THROW) {
    sd->tok.type = TOKEN_KEYWORD;
    record_walk(sd, 0);

    // throw doesn't cause ASI, because it's invalid either way
    if (outer_hash == LIT_RETURN && yield_restrict_asi(sd)) {
      return 0;
    }

    goto block_bail;  // e.g. "return ..." or "throw ..." starts a statement
  }

  // module valid cases at top-level
  if (sd->curr == sd->stack && sd->is_module) {

    // match "import" which starts a sstack special
    if (outer_hash == LIT_IMPORT) {
      sd->tok.type = TOKEN_KEYWORD;
      record_walk(sd, 0);

      // short-circuit for "import 'foo'"
      if (sd->tok.type == TOKEN_STRING) {
        sd->tok.mark = MARK_IMPORT;
        goto block_bail;  // starts a statement containing at least one string
      }

      stack_inc(sd, SSTACK__MODULE);
      return 0;
    }

    // match "export" which is sort of a no-op, resets to default state
    if (outer_hash == LIT_EXPORT) {
      sd->tok.type = TOKEN_KEYWORD;
      record_walk(sd, 0);

      if (sd->tok.hash == MISC_STAR || sd->tok.type == TOKEN_BRACE) {
        stack_inc(sd, SSTACK__MODULE);
        return 0;
      }

      if (sd->tok.hash == LIT_DEFAULT) {
        sd->tok.type = TOKEN_KEYWORD;
        record_walk(sd, 0);
      }

      // interestingly: "export default function() {}" is valid and a decl
      // so classic JS rules around decl must have names are ignored
      // ... "export default if (..)" is invalid, so we don't care if you do wrong things after
      return 0;
    }

  }

  // match "var", "let" and "const"
  if (match_decl(sd) >= 0) {
    goto block_bail;  // matched, now rest is regular statement
  }

  // match e.g., "if", "catch"
  if (outer_hash & _MASK_CONTROL) {
    stack_inc(sd, SSTACK__CONTROL);
    sd->curr->start = outer_hash;

    sd->tok.type = TOKEN_KEYWORD;
    record_walk(sd, 0);

    // match "for await"
    if (outer_hash == LIT_FOR && sd->tok.hash == LIT_AWAIT) {
      // even outside strict/async mode, this is valid, but an error
      sd->tok.type = TOKEN_KEYWORD;
      skip_walk(sd, 0);
    }

    // if we need a paren, consume and create expr group
    if ((outer_hash & _MASK_CONTROL_PAREN) && sd->tok.type == TOKEN_PAREN) {
      record_walk(sd, -1);
      stack_inc(sd, SSTACK__EXPR);
      sd->curr->start = TOKEN_PAREN;
    }

    return 0;
  }

  // hoisted function or class
  if (enact_defn(sd)) {
    return 0;
  }

block_bail:
  // start a regular statement
  stack_inc(sd, SSTACK__EXPR);
  return 0;
}


#ifdef DEBUG
void render_token(token *out, char *start) {
  if (!out->type) {
    return;
  }
  char c = ' ';
  if (out->type == TOKEN_SEMICOLON && !out->len) {
    c = ';';  // this is an ASI
  } else if (out->hash) {
    c = '#';  // has a hash
  }
  int at = 0;
  if (out->p) {
    at = out->p - start;
  }
  printf("%c%4d.%02d: %.*s [%d]\n", c, out->line_no, out->type, out->len, out->p, at);
}
#endif


int prsr_simple(tokendef *td, int is_module, prsr_callback cb, void *arg) {
#ifdef DEBUG
  char *start = td->buf;
#endif
  simpledef sd;
  bzero(&sd, sizeof(simpledef));
  sd.curr = sd.stack;
  sd.is_module = is_module;
  if (is_module) {
    sd.curr->context = CONTEXT__STRICT;
  }
  sd.td = td;
  sd.next = &(td->next);
  sd.cb = cb;
  sd.arg = arg;

  sd.curr->stype = SSTACK__BLOCK;
  record_walk(&sd, -1);
  sd.curr->prev.type = TOKEN_TOP;

  int unchanged = 0;
  int ret = 0;
  while (sd.tok.type) {
    char *prev = sd.tok.p;
    ret = simple_consume(&sd);
    if (ret) {
      break;
    }

    // check stack range
    int depth = sd.curr - sd.stack;
    if (depth >= __STACK_SIZE - 1 || depth < 0) {
      debugf("stack exception, depth=%d\n", depth);
      ret = ERROR__STACK;
      break;
    }

    // allow unchanged ptr for some attempts for state machine
    if (prev == sd.tok.p) {
      if (unchanged++ < 4) {
        // we give it four chances to change something to let the state machine work
        // (needed for SSTACK__CONTROL)
        continue;
      }
      debugf("simple_consume didn't consume: %d %.*s\n", sd.tok.type, sd.tok.len, sd.tok.p);
      ret = ERROR__INTERNAL;
      break;
    }

    // success
    prev = sd.tok.p;
    unchanged = 0;
  }

  if (ret) {
    return ret;
  }

  char *eof_at = sd.tok.p;
  sd.tok.p = 0;

  // handle open ASYNC state
  if (sd.curr->stype == SSTACK__ASYNC) {
    debugf("end: sending fake TOKEN_EOF\n");
    sd.tok.type = TOKEN_EOF;
    simple_consume(&sd);
  }

  // consume fake TOKEN_CLOSE in a few cases for ASI
  int stype = sd.curr->stype;
  if (stype == SSTACK__EXPR && (sd.curr - 1)->stype == SSTACK__BLOCK) {
    debugf("end: sending fake TOKEN_CLOSE\n");
    sd.tok.type = TOKEN_CLOSE;
    simple_consume(&sd);
  }

  // close any open virtual control/exec pairs
  if ((sd.curr->stype == SSTACK__BLOCK && maybe_close_control(&sd, NULL)) ||
      sd.curr->stype == SSTACK__CONTROL) {
    debugf("end: closing virtual pairs\n");
    simple_consume(&sd);  // token doesn't matter, SSTACK__CONTROL doesn't consume
  }

  // emit 'real' EOF for caller
  sd.tok.type = TOKEN_EOF;
  sd.tok.p = eof_at;
  skip_walk(&sd, -1);

  if (sd.curr != sd.stack) {
#ifdef DEBUG
    debugf("stack is %ld too high\n", sd.curr - sd.stack);
    sstack *t = sd.stack;
    do {
      debugf("stack=%ld stype=%d\n", t - sd.stack, t->stype);
      render_token(&(t->prev), start);
    } while (t != sd.curr && ++t);
#endif
    return ERROR__STACK;
  }
  return 0;
}
