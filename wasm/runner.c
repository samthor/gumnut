#include <string.h>
#include "../token.h"
#include "../parser.h"

static tokendef shared_token;
static token out;

int prsr_setup(char *buf) {
  bzero(&shared_token, sizeof(shared_token));
  shared_token.buf = buf;
  shared_token.len = strlen(buf);
  shared_token.depth = 1;
  shared_token.line_no = 1;
  return 0;
}

int prsr_run() {
  return prsr_next_token(&shared_token, &out);
}

int prsr_get_at() {
  return out.p - shared_token.buf;
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
