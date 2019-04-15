#include <string.h>
#include "../token.h"
#include "../simple/simple.h"

#include <emscripten.h>

extern void token_callback(int p, int len, int line_no, int type, int mark);

void internal_callback(void *arg, token *out) {
  // arg is our shared_td
  tokendef *td = (tokendef *) arg;
  int p = out->p - td->buf;
  token_callback(p, out->len, out->line_no, out->type, out->mark);
}

static tokendef shared_td;
static int shared_is_module = 0;

EMSCRIPTEN_KEEPALIVE
int prsr_setup(char *buf, int is_module) {
  shared_td = prsr_init_token(buf);
  shared_is_module = is_module;
  return 0;
}

EMSCRIPTEN_KEEPALIVE
int prsr_run() {
  // TODO: make this yield a few tokens at a time
  return prsr_simple(&shared_td, shared_is_module, internal_callback, &shared_td);
}
