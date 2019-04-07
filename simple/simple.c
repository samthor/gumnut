
#include <string.h>
#include "simple.h"
#include "../utils.h"

typedef struct {

  token t1;
  token t2;
  uint8_t t3type : 5;

  uint8_t is_block : 1;
  uint8_t is_decl : 1;
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

static int is_optional_keyword(sstack *dep) {
  if (token_string(&(dep->t1), "await", 5)) {
    // TODO: check async
    printf("foolishly allowing 'await'\n");
    return 1;
  }

  if (token_string(&(dep->t1), "yield", 5)) {
    // TODO: check generator
    printf("foolishly allowing 'yield'\n");
    return 1;
  }

  return 0;
}

static int token_was_class(token *t) {
  return t->type == TOKEN_INTERNAL && t->p[0] == 'c';
}

static int brace_is_block(sstack *dep, int line_no) {
  if (dep->is_decl && (dep - 1)->t1.p[0] == 'c') {
    // if we're a decl and parent is 'class', this is a dict
    return 0;
  }

  if (token_was_class(&(dep->t1)) || token_was_class(&(dep->t2))) {
    // we're a brace immediately after "class" or "class extends X", this is block
    return 0;
  }

  switch (dep->t1.type) {
    case TOKEN_COLON:
      // (weird but valid) after a label, e.g. "foo: {}"
      // ALT: inside dict OR inside ?: combo
      return dep->is_block && dep->t2.type == TOKEN_LABEL;

    case TOKEN_EOF:    // start of level, e.g. "[ X"
      return dep->is_block;

    case TOKEN_OP:
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

      if (is_asi_change(dep->t1.p, dep->t1.len)) {
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
    case TOKEN_EOF:
      return 0;

    case TOKEN_PAREN:
      if (dep->t2.type == TOKEN_LIT && is_control_paren(dep->t2.p, dep->t2.len)) {
        return 0;
      }
      return 1;

    case TOKEN_BRACE:
      if (dep->t3type == TOKEN_INTERNAL && dep->t2.type == TOKEN_PAREN) {
        // 'function' '()' '{}'...
        return 1;  // nb. this does NOT fire for hoisted since they don't leave paren/brace
      }

      if (!(dep + 1)->is_block || !dep->is_block) {
        return 1;  // prev was dict OR we are non-block
      }

      return 0;

    case TOKEN_LIT:
      if (is_optional_keyword(dep)) {
        return 0;
      }

      return !is_operates(dep->t1.p, dep->t1.len);

    case TOKEN_STRING:
    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
      return 1;
  }

  return 0;
}

static int hoist_is_decl(sstack *dep, int line_no) {
  if (!dep->is_block) {
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

static void process_function(simpledef *sd) {
  token *next = &(sd->td->next);
  int is_async = (sd->curr->t1.type == TOKEN_LIT && token_string(&(sd->curr->t1), "async", 5));
  int is_generator = 0;

  // peek for generator star
  if (next->type == TOKEN_OP && next->len == 1 && next->p[0] == '*') {
    read_next(sd, 0);
    is_generator = 1;
  }

  // peek for function name
  if (next->type == TOKEN_LIT) {
    read_next(sd, 0);  // FIXME: we can emit this as a lit?
  }

  // FIXME: do something with is_generator + is_async
  // FIXME: need this for => and methods in dicts
  printf("found function async=%d generator=%d\n", is_async, is_generator);
}

static void process_class(simpledef *sd) {
  token *next = &(sd->td->next);

  // peek for name
  if (next->type == TOKEN_LIT && !token_string(next, "extends", 7)) {
    read_next(sd, 0);  // FIXME: we can emit this as a lit?
    printf("... name=%.*s\n", sd->tok.len, sd->tok.p);
  }

  // peek for extends
  if (next->type == TOKEN_LIT && token_string(next, "extends", 7)) {
    read_next(sd, 0);  // FIXME: we can emit this as a keyword?
  }
}

static int simple_step(simpledef *sd) {
  sstack *dep = sd->curr;
  uint8_t is_block = 0;

  switch (sd->tok.type) {
    case TOKEN_CLOSE:
      --sd->curr;  // pop stack

      if (sd->curr->is_decl && sd->curr->t1.type == TOKEN_BRACE) {
        // ... if we're at decl level and just had a brace open/close
        --sd->curr;
      }

      return -1;    // nothing else to do, don't record

    case TOKEN_BRACE:
      is_block = brace_is_block(dep, sd->tok.line_no);
      printf("brace_is_block: %d\n", is_block);
      // fall-through

    case TOKEN_ARRAY:
    case TOKEN_PAREN:
    case TOKEN_T_BRACE:
      ++sd->curr;
      bzero(sd->curr, sizeof(sstack));
      sd->curr->is_block = is_block;
      return 0;

    case TOKEN_OP:
      // if this ++/-- attaches to left, don't record: shouldn't change value-ness
      if (is_double_addsub(sd->tok.p, sd->tok.len) && stack_has_value(dep)) {
        return -1;
      }
      return 0;

    case TOKEN_LIT:
      break;  // continue below

    default:
      return 0;
  }

  int is_decl = hoist_is_decl(dep, sd->tok.line_no);
  if (is_decl) {
    // match labels at top-level
    if (sd->td->next.type == TOKEN_COLON) {
      if (!is_reserved_word(sd->tok.p, sd->tok.len)) {
        // TODO(samthor): generate error if reserved word?
        sd->tok.type = TOKEN_LABEL;
      }
      return 0;
    }
  }

  if (!is_hoist_keyword(sd->tok.p, sd->tok.len)) {
    return 0;
  }

  // if this is a hoisted (declaration) class/function, increase level: this leaves TOKEN_INTERNAL
  // marker in the "regular flow", but we ignore it (no value as decl)
  if (is_decl) {
    dep = ++sd->curr;
    bzero(sd->curr, sizeof(sstack));
    dep->is_decl = 1;
  }

  token fake = sd->tok;  // process_... consumes tokens, so copy
  if (fake.p[0] == 'f') {
    process_function(sd);
  } else {
    process_class(sd);
  }
  fake.type = TOKEN_INTERNAL;
  sd->tok = fake;  // pretend the consumed tokens were a single TOKEN_INTERNAL

  return 0;
}

int prsr_simple(tokendef *td, prsr_callback cb, void *arg) {
  simpledef sd;
  bzero(&sd, sizeof(simpledef));
  sd.curr = sd.stack;
  sd.td = td;
  sd.cb = cb;
  sd.arg = arg;
  sd.curr->is_block = 1;

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

    dep->t3type = dep->t2.type;
    dep->t2 = dep->t1;
    dep->t1 = sd.tok;
  }

  return 0;
}