
#include <string.h>
#include "simple.h"
#include "../utils.h"

#define SSTACK_ROOT       1  // top-level
#define SSTACK_INTERNAL   2  // function or class
#define SSTACK_BLOCK      3
#define SSTACK_DECL       4

// for SSTACK_INTERNAL mode='c'
#define FLAG_EXTENDS_BRACE   1

// for SSTACK_INTERNAL mode='f'
#define FLAG_ASYNC           1
#define FLAG_GENERATOR       2

// for empty SSTACK (but unique for safety)
#define FLAG_DICT_VALUE      4  // between ':' and ',' in dict

typedef struct {
  token t1;
  token t2;
  uint8_t stype : 4;
  uint8_t flags : 4;
} sstack;

typedef struct {
  tokendef *td;
  token tok;

  prsr_callback cb;
  void *arg;

  sstack *curr;
  sstack stack[256];
} simpledef;

static int token_string(token *t, char *s, int len) {
  return t->len == len && !memcmp(t->p, s, len);
}

static char sstack_internal_mode(sstack *dep) {
  if (dep->stype == SSTACK_INTERNAL) {
    return (dep - 1)->t1.p[0];
  }
  return 0;
}

static int sstack_is_dict(sstack *dep) {
  return !dep->stype && (dep - 1)->t1.type == TOKEN_BRACE;
}

static int is_optional_keyword(sstack *dep) {
  uint8_t mask = 0;

  if (token_string(&(dep->t1), "await", 5)) {
    mask = FLAG_ASYNC;
  } else if (token_string(&(dep->t1), "yield", 5)) {
    mask = FLAG_GENERATOR;
  } else {
    return 0;
  }

  // look up for mask
  // TODO(samthor): we don't record object-functions
  for (sstack *p = dep; p->stype != SSTACK_ROOT; --p) {
    char mode = sstack_internal_mode(p);
    if (mode == 'f') {
      return p->flags & mask;
    }
  }

  return 0;
}

static int brace_is_block(sstack *dep, int line_no) {
  char mode = sstack_internal_mode(dep);
  if (mode) {
    return mode == 'f';
  }

  switch (dep->t1.type) {
    case TOKEN_EOF:    // start of level, e.g. "[ X"
    case TOKEN_COLON:  // "foo: {}" for block, "{key: {}}" in dict-like
      return dep->stype == SSTACK_BLOCK;

    case TOKEN_COMMA:
    case TOKEN_SPREAD:   // nonsensical, but valid: "[...{}]"
    case TOKEN_OP:
    case TOKEN_TERNARY:  // following a "? ... :" stack
      return 0;

    case TOKEN_LIT:
      if (is_optional_keyword(dep)) {
        if (dep->t1.p[0] == 'y') {
          // yield is a restricted keyword
          return line_no != dep->t1.line_no;
        }
        return 0;
      }

      // nb. don't bother with `import var let const`, their grammar is limited anyway

      if (token_string(&(dep->t1), "return", 6)) {
        return line_no != dep->t1.line_no;   // return \n { if () { ... } }
      }

      if (is_dict_after(dep->t1.p, dep->t1.len)) {
        return 0;
      }

      break;
  }

  return 1;
}

static int stack_has_value(sstack *dep) {
  switch (dep->t1.type) {
    case TOKEN_INTERNAL:
      return 1;  // if this was a decl, then TOKEN_INTERNAL wouldn't be in stream

    case TOKEN_EOF:
      return 0;

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
        return 0;
      }

      return !is_always_operates(dep->t1.p, dep->t1.len);

    case TOKEN_STRING:
    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
      return 1;
  }

  return 0;
}

static int hoist_is_decl(sstack *dep, int line_no) {
  if (!dep->stype) {
    return 0;
  }

  // if (stack_has_value(dep)) {
  //   return 1;
// TODO: same line is syntax error, so whatever?!
//    return line_no != dep->t1.line_no;
    // same line is syntax error, e.g.:
    //   "1 function bar() {}"
    //   "+{} class Foo {}"
    // next line is valid, but needs ASI (?):
    //   "1
    //   function foo() {}"
  // }

  int out = brace_is_block(dep, line_no);
  if (!out) {
    if (stack_has_value(dep)) {
      printf("GOT INVARIANT BREAK: has value, but would not be block\n");
      return 1;
    }
  }

  return out;
}

static sstack *stack_inc(simpledef *sd, uint8_t stype) {
  // TODO: check bounds
  ++sd->curr;
  bzero(sd->curr, sizeof(sstack));
  sd->curr->stype = stype;
  return sd->curr;
}

static int read_next(simpledef *sd, int has_value) {
  do {
    // prsr_next_token can reveal comments, loop until over them
    int out = prsr_next_token(sd->td, &(sd->tok), has_value);
    if (out) {
      return out;
    }
    sd->cb(sd->arg, &(sd->tok));
  } while (sd->tok.type == TOKEN_COMMENT);
  return 0;
}

static uint8_t process_function(simpledef *sd) {
  token *next = &(sd->td->next);
  uint8_t flags = 0;
  if (sd->curr->t1.type == TOKEN_LIT && token_string(&(sd->curr->t1), "async", 5)) {
    flags = FLAG_ASYNC;
  }

  // peek for generator star
  if (next->type == TOKEN_OP && next->len == 1 && next->p[0] == '*') {
    read_next(sd, 0);
    flags |= FLAG_GENERATOR;
  }

  // peek for function name
  if (next->type == TOKEN_LIT) {
    read_next(sd, 0);  // FIXME: we can emit this as a lit?
  }

  return flags;
}

static uint8_t process_class(simpledef *sd) {
  token *next = &(sd->td->next);
  uint8_t flags = 0;

  // peek for name
  if (next->type == TOKEN_LIT && !token_string(next, "extends", 7)) {
    read_next(sd, 0);  // FIXME: we can emit this as a lit?
  }

  // peek for extends
  if (next->type == TOKEN_LIT && token_string(next, "extends", 7)) {
    read_next(sd, 0);  // FIXME: we can emit this as a keyword?

    if (next->type == TOKEN_BRACE) {
      // "class Foo extends {} {}" is nonsensical but valid; record it so both braces are dicts
      flags = FLAG_EXTENDS_BRACE;
    }
  }

  return flags;
}

static sstack *stack_inc_fakefunction(simpledef *sd, uint8_t flags) {
  static const char *functionStr = "f";

  token fake;
  bzero(&fake, sizeof(token));
  fake.type = TOKEN_INTERNAL;
  fake.p = (char *) functionStr;
  fake.len = 1;

  sd->tok = fake;  // caller writes us to t1 in the regular flow
  sstack *dep = stack_inc(sd, SSTACK_INTERNAL);
  if (dep) {
    dep->flags = flags;
  }
  return dep;
}

static int simple_step(simpledef *sd) {
  uint8_t stype = 0;

  // left-side of dictionary
  if (sstack_is_dict(sd->curr) && !(sd->curr->flags & FLAG_DICT_VALUE)) {
    uint8_t flags = 0;

    // look for 'async' without following '('
    if (sd->tok.type == TOKEN_LIT &&
        sd->td->next.type != TOKEN_PAREN &&
        token_string(&(sd->tok), "async", 5)) {
      // found, consume
      flags |= FLAG_ASYNC;
      read_next(sd, 0);
    }

    // look for '*'
    if (sd->tok.type == TOKEN_OP && token_string(&(sd->tok), "*", 1)) {
      flags |= FLAG_GENERATOR;
      read_next(sd, 0);
    }

    // if we found something OR the next would open a paren (end of def), mark as a function
    if (flags || sd->td->next.type == TOKEN_PAREN) {
      // ... this replaces "async * foo" with a TOKEN_INTERNAL in stream
      stack_inc_fakefunction(sd, flags);
      return 0;
    }
  }

  switch (sd->tok.type) {
    case TOKEN_ARROW:
      if (!(sd->curr->t1.type == TOKEN_PAREN || sd->curr->t1.type == TOKEN_LIT)) {
        return 0;  // not a valid construct
      }

      uint8_t flags = 0;
      if (sd->curr->t2.type == TOKEN_LIT && token_string(&(sd->curr->t2), "async", 5)) {
        flags = FLAG_ASYNC;
      }

      if (sd->td->next.type == TOKEN_BRACE) {
        // the sensible arrow function case, with a proper body
        // e.g. "() => { statements }"
        // ... this replaces "=>" with a TOKEN_INTERNAL in stream
        stack_inc_fakefunction(sd, flags);
        return 0;
      }

      // TODO: push for {} or single statement :(
      printf("found arrow with statement only :(, flags=%d\n", flags);
      return 0;

    case TOKEN_CLOSE:
      --sd->curr;  // pop stack
      if (sd->curr->t1.type != TOKEN_BRACE) {
        return -1;
      }

      char mode = sstack_internal_mode(sd->curr);
      if (!mode) {
        return -1;  // random {}, not a special mode
      }

      // "class Foo extends {} {}" is nonsensical but valid; first brace ended
      if (mode == 'c' && sd->curr->flags == FLAG_EXTENDS_BRACE) {
        sd->curr->flags = 0;
        return -1;
      }
      --sd->curr;  // class or function done

      // ... also ended a decl
      if (sd->curr->stype == SSTACK_DECL) {
        --sd->curr;
      }
      return -1;    // nothing else to do, don't record

    case TOKEN_BRACE:
      if (brace_is_block(sd->curr, sd->tok.line_no)) {
        stype = SSTACK_BLOCK;
      }
      printf("brace_is_block: %d\n", stype == SSTACK_BLOCK);
      // fall-through

    case TOKEN_TERNARY:
    case TOKEN_ARRAY:
    case TOKEN_PAREN:
    case TOKEN_T_BRACE:
      stack_inc(sd, stype);
      return 0;

    case TOKEN_OP:
      // if this ++/-- attaches to left, don't record: shouldn't change value-ness
      if (is_double_addsub(sd->tok.p, sd->tok.len) && stack_has_value(sd->curr)) {
        return -1;
      }
      return 0;

    case TOKEN_COMMA:
      // nb. FLAG_DICT_VALUE is unique so we don't check sstack_is_dict
      if (sd->curr->flags & FLAG_DICT_VALUE) {
        sd->curr->flags &= ~FLAG_DICT_VALUE;
      }
      return 0;

    case TOKEN_COLON:
      // nb. only shows up if not part of "? ... :"
      if (!sstack_is_dict(sd->curr)) {
        return 0;
      }
      sd->curr->flags = FLAG_DICT_VALUE;
      return 0;

    case TOKEN_LIT:
      break;  // continue below

    default:
      return 0;
  }

  sstack *dep = sd->curr;
  int is_decl = hoist_is_decl(dep, sd->tok.line_no);
  if (is_decl) {
    // match labels at top-level
    if (sd->td->next.type == TOKEN_COLON) {
      if (!is_reserved_word(sd->tok.p, sd->tok.len)) {
        // FIXME: Output has already been generated at this point.
        // TODO(samthor): generate error if reserved word?
        sd->tok.type = TOKEN_LABEL;
      }
      return 0;
    }
  }

  if (!is_hoist_keyword(sd->tok.p, sd->tok.len)) {
    return 0;
  }

  token fake = sd->tok;  // process_... consumes tokens, so copy
  uint8_t flags;
  if (fake.p[0] == 'f') {
    flags = process_function(sd);
  } else {
    flags = process_class(sd);
  }
  fake.type = TOKEN_INTERNAL;

  if (is_decl) {
    // if this is hoisted, then don't leave anything in the regular flow
    // it's like we were never here!
    dep = stack_inc(sd, SSTACK_DECL);

    // ... but write inside DECL, before SSTACK_INTERNAL
    dep->t1 = fake;
    dep = stack_inc(sd, SSTACK_INTERNAL);
    dep->flags = flags;
    return -1;  // don't write us
  }

  sd->tok = fake;  // caller writes us to t1 in the regular flow
  dep = stack_inc(sd, SSTACK_INTERNAL);
  dep->flags = flags;
  return 0;
}

int prsr_simple(tokendef *td, prsr_callback cb, void *arg) {
  simpledef sd;
  bzero(&sd, sizeof(simpledef));
  sd.curr = sd.stack;
  sd.td = td;
  sd.cb = cb;
  sd.arg = arg;

  sd.curr->stype = SSTACK_ROOT;
  ++sd.curr;
  sd.curr->stype = SSTACK_BLOCK;

  for (;;) {
    int has_value = 1;
    if (td->next.type == TOKEN_SLASH) {
      has_value = stack_has_value(sd.curr);
    }

    int out = read_next(&sd, has_value);
    if (out) {
      return out;
    }
    if (sd.tok.type == TOKEN_EOF) {
      break;
    }

    sstack *dep = sd.curr;

    int skip = simple_step(&sd);
    if (skip) {
      continue;  // don't record in t1/t2
    }

    dep->t2 = dep->t1;
    dep->t1 = sd.tok;
  }

  return 0;
}