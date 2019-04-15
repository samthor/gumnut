#include <string.h>
#include "../token.h"
#include "../simple/simple.h"

#include <emscripten.h>

extern void token_callback(int p, int len, int line_no, int type);

void internal_callback(void *arg, token *out) {
  // arg is our shared_td
  tokendef *td = (tokendef *) arg;
  int p = out->p - td->buf;
  token_callback(p, out->len, out->line_no, out->type);
}

static tokendef shared_td;

EMSCRIPTEN_KEEPALIVE
int prsr_setup(char *buf) {
  shared_td = prsr_init_token(buf);
  return 0;
}

EMSCRIPTEN_KEEPALIVE
int prsr_run(int strict) {
  int context = (strict ? CONTEXT__STRICT : 0);

  // TODO: make this yield a few tokens at a time
  return prsr_simple(&shared_td, 0, internal_callback, &shared_td);
}
