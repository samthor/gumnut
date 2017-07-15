#include <string.h>
#include "../token.h"
#include "../parser.h"

static parserdef shared_parser;
static token out;

int prsr_setup(char *buf) {
  prsr_parser_init(&shared_parser, buf);
  return 0;
}

int prsr_run() {
  return prsr_next(&shared_parser, &out);
}

int prsr_get_at() {
  return out.p - shared_parser.td.buf;
}

int prsr_get_len() {
  return out.len;
}

int prsr_get_line_no() {
  return out.line_no;
}

int prsr_get_type() {
  return out.type;
}
