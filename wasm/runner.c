#include <string.h>
#include "../token.h"
#include "../parser.h"

// this is just EMSCRIPTEN_KEEPALIVE from the actual source
#define EXPORT __attribute__((used))

static parserdef shared_parser;
static token out;

EXPORT
int prsr_setup(char *buf) {
  prsr_parser_init(&shared_parser, buf);
  return 0;
}

EXPORT
int prsr_run() {
  return prsr_next(&shared_parser, &out);
}

EXPORT
int prsr_get_at() {
  return out.p - shared_parser.td.buf;
}

EXPORT
int prsr_get_len() {
  return out.len;
}

EXPORT
int prsr_get_invalid() {
  return out.invalid;
}

EXPORT
int prsr_get_line_no() {
  return out.line_no;
}

EXPORT
int prsr_get_type() {
  return out.type;
}
