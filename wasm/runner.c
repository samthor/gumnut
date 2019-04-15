#include <string.h>
#include "../token.h"
#include "../simple/simple.h"

#include <emscripten.h>

extern void token_callback(void *arg, char *p, int len, int line_no, int type, int mark);

void internal_callback(void *arg, token *out) {
  // arg is our runnerdef
  token_callback(arg, out->p, out->len, out->line_no, out->type, out->mark);
}

typedef struct {
  tokendef td;
  int is_module;
} runnerdef;

// number of bytes wasm must give us to store parser state
EMSCRIPTEN_KEEPALIVE
int prsr_size() {
  return sizeof(runnerdef);
}

// setup, assumed allocated prsr_size() bytes for us
EMSCRIPTEN_KEEPALIVE
int prsr_setup(void *at, char *buf, int is_module) {
  runnerdef *rd = (runnerdef *) at;
  rd->td = prsr_init_token(buf);
  rd->is_module = is_module;
  return 0;
}

EMSCRIPTEN_KEEPALIVE
int prsr_run(void *at) {
  runnerdef *rd = (runnerdef *) at;

  // TODO: make this yield a few tokens at a time
  return prsr_simple(&(rd->td), rd->is_module, internal_callback, at);
}
