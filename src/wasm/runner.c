#include "../parser.h"

#include <emscripten.h>
#include <stdlib.h>
#include <assert.h>

static token t;

// Confirm struct padding as the JS uses it to read values directly.
static_assert(sizeof(token) == 20, "Token should be 20 bytes");
static_assert(__builtin_offsetof(token, p) == 0, "p=0");
static_assert(__builtin_offsetof(token, len) == 4, "len=4");
static_assert(__builtin_offsetof(token, line_no) == 8, "line_no=8");
static_assert(__builtin_offsetof(token, type) == 12, "type=12");
static_assert(__builtin_offsetof(token, hash) == 16, "hash=16");

static token *out = 0;

extern void token_callback(int special);

static void internal_callback(int special) {
  token_callback(special);
}

// setup, assumed allocated prsr_size() bytes for us
EMSCRIPTEN_KEEPALIVE
void *xx_setup(char *p) {
  out = modp_init(p, 0, internal_callback);
  return (void *) out;
}

EMSCRIPTEN_KEEPALIVE
int xx_run() {
  return modp_run();
}
