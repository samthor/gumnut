#include "../parser.h"

#include <emscripten.h>

extern void token_callback(char *p, int len, int line_no, int type, int special);

static token *out = 0;

static void internal_callback(int special) {
  token_callback(out->p, out->len, out->line_no, out->type, special);
}

// setup, assumed allocated prsr_size() bytes for us
EMSCRIPTEN_KEEPALIVE
void xx_setup(char *p) {
  out = modp_init(p, 0, internal_callback);
}

EMSCRIPTEN_KEEPALIVE
int xx_run() {
  return modp_run();
}
