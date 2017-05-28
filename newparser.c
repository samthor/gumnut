#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include "token.h"
#include "utils.h"

int prsr_token(char *buf, int (*fp)(token *)) {
  tokendef d;
  memset(&d, 0, sizeof(d));
  d.buf = buf;
  d.len = strlen(buf);
  d.line_no = 1;

  token out;
  int ret;
  while (!(ret = prsr_next_token(&d, &out))) {

    switch (out.type) {
    case TOKEN_KEYWORD:
      if (is_asi_keyword(out.p, out.len)) {
        printf("got ASI keyword, blah\n");
      }
      break;
    }

    fp(&out);
  }
  return ret;
}
