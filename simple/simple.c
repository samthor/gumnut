
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



static int brace_is_block(sstack *dep, int line_no) {
  switch (dep->t1.type) {
    case TOKEN_EOF:    // start of level, e.g. "[ X"
    case TOKEN_COLON:  // e.g. {foo: X}, label (block) or ident (dict)
      return dep->is_block;

    case TOKEN_OP:
      return 0;

    case TOKEN_LIT:
      if (is_asi_change(dep->t1.p, dep->t1.len)) {
        if (line_no != dep->t1.line_no) {
          return 1;  // return \n {}
        }
        return 0;
      } else if (is_oplike(dep->t1.p, dep->t1.len) || is_case(dep->t1.p, dep->t1.len)) {
        return 0;
      }
      break;
  }

  return 1;
}

static int simple_normal_step(simpledef *sd) {
  int out = prsr_next_token(sd->td, &(sd->tok), 0);
  if (out) {
    return out;
  }
  sstack *dep = sd->curr;

   switch (sd->tok.type) {
    case TOKEN_COMMENT:
      return 0;    // pending comment, don't record, just yield

    case TOKEN_CLOSE:
      bzero(sd->curr, sizeof(sstack));
      --sd->curr;  // pop stack
      return 0;    // nothing else to do, don't record

    case TOKEN_ARRAY:
    case TOKEN_PAREN:
    case TOKEN_T_BRACE:
      ++sd->curr;
      break;

    case TOKEN_BRACE:
      ++sd->curr;
      sd->curr->is_block = brace_is_block(dep, sd->tok.line_no);
      printf("got block? %d\n", sd->curr->is_block);
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
    if (td->next.type != TOKEN_SLASH) {
      int out = simple_normal_step(&sd);
      if (out) {
        return out;
      }
      cb(arg, &(sd.tok));
      if (sd.tok.type == TOKEN_EOF) {
        break;
      }

    } else {
      printf("unhandled slash\n");

      // TODO: determine, but then loop while comment found

      return -1;
    }
  }

  return 0;
}