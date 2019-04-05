
#include <string.h>
#include "simple.h"
#include "../utils.h"

typedef struct {
  uint8_t type : 5;
  uint8_t is_block : 1;

  token t1;
  token t2;

} sstack;

typedef struct {
  tokendef *td;
  token tok;

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

static int brace_is_block(sstack *dep, int line_no) {
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
      if (!(dep + 1)->is_block || !dep->is_block) {
        return 1;  // prev was dict OR we are non-block
      }

      // TODO: if _declaration_ of class or fn, has no value
      // if _statement_, has value
      // ... persist this beyond t1/t2 etc, can be "long"

      printf("unknown decl/stmt case: %d\n", dep->t2.type);
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

static int simple_normal_step(simpledef *sd) {
  int out = prsr_next_token(sd->td, &(sd->tok), 0);
  if (out) {
    return out;
  }
  sstack *dep = sd->curr;
  uint8_t is_block = 0;

  switch (sd->tok.type) {
    case TOKEN_COMMENT:
      return 0;    // pending comment, don't record, just yield

    case TOKEN_CLOSE:
      --sd->curr;  // pop stack
      return 0;    // nothing else to do, don't record

    case TOKEN_BRACE:
      is_block = brace_is_block(dep, sd->tok.line_no);
      // fall-through

    case TOKEN_ARRAY:
    case TOKEN_PAREN:
    case TOKEN_T_BRACE:
      ++sd->curr;
      bzero(sd->curr, sizeof(sstack));
      sd->curr->is_block = is_block;
      break;

    case TOKEN_OP:
      // if this ++/-- attaches to left, don't record: shouldn't change value-ness
      if (is_double_addsub(sd->tok.p, sd->tok.len) && stack_has_value(dep)) {
        return 0;
      }
      break;

    case TOKEN_LIT:
      if (!hoist_is_decl(dep, sd->tok.line_no)) {
        break;
      }

      // FIXME: look for '[async] function' or 'class'

      if (is_hoist_keyword(sd->tok.p, sd->tok.len)) {
        // TODO: consume
        printf("found hoisted: %.*s\n", sd->tok.len, sd->tok.p);

      } else if (sd->td->next.type == TOKEN_COLON && !is_reserved_word(sd->tok.p, sd->tok.len)) {
        // TODO(samthor): generate error if reserved word?
        sd->tok.type = TOKEN_LABEL;
      }

      break;

  }

  dep->t2 = dep->t1;
  dep->t1 = sd->tok;
  return 0;
}

int prsr_simple(tokendef *td, prsr_callback cb, void *arg) {
  simpledef sd;
  bzero(&sd, sizeof(simpledef));
  sd.curr = sd.stack;
  sd.td = td;
  sd.curr->is_block = 1;

  for (;;) {
    int out;

    if (td->next.type != TOKEN_SLASH) {
      out = simple_normal_step(&sd);
      if (out) {
        return out;
      }
      cb(arg, &(sd.tok));
      if (sd.tok.type == TOKEN_EOF) {
        break;
      }
      continue;
    }

    int has_value = stack_has_value(sd.curr);
    do {
      // next_token can reveal comments, loop until over them
      out = prsr_next_token(td, &(sd.tok), has_value);
      if (out) {
        return out;
      }
      cb(arg, &(sd.tok));
    } while (sd.tok.type == TOKEN_COMMENT);

    // TODO: combine with simple?
    sd.curr->t2 = sd.curr->t1;
    sd.curr->t1 = sd.tok;
  }

  return 0;
}