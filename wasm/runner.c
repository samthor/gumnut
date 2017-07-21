#include <string.h>
#include "../token.h"
#include "../parser.h"

#include <emscripten.h>

static parserdef shared_parser;
static token out;

EMSCRIPTEN_KEEPALIVE
int prsr_setup(char *buf) {
  prsr_parser_init(&shared_parser, buf);
  return 0;
}

EMSCRIPTEN_KEEPALIVE
int prsr_run() {
  return prsr_next(&shared_parser, &out);
}

EMSCRIPTEN_KEEPALIVE
int prsr_get_at() {
  return out.p - shared_parser.td.buf;
}

EMSCRIPTEN_KEEPALIVE
int prsr_get_len() {
  return out.len;
}

EMSCRIPTEN_KEEPALIVE
int prsr_get_invalid() {
  return out.invalid;
}

EMSCRIPTEN_KEEPALIVE
int prsr_get_line_no() {
  return out.line_no;
}

EMSCRIPTEN_KEEPALIVE
int prsr_get_type() {
  return out.type;
}
