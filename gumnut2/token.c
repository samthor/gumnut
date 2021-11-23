
#include "token.h"
#include "debug.h"
#include "types.h"

#include <strings.h>


static struct gumnut_internal_t _td;
#define td (&_td)


static struct token_t *first = td->buf;


#include "_consume.c"
#include "_tables.c"
#include "tokens/helper.c"


int gumnut_init(char *p, int len) {
  bzero(td, sizeof(struct gumnut_internal_t));

  td->void_at = p;
  td->end = p + len;
  td->line_no = 1;

  // we hack this to pretend we're "done" with the first token
  td->buf_have = 1;

  // // depth is length of stack, check (td->depth - 1)
  // td->stack[0].open = TOKEN_BLOCK;
  // td->depth = 1;

  // sanity-check td->end is NULL
  if (len < 0 || td->end[0]) {
    debugf("got bad td->end");
    return ERROR__UNEXPECTED;
  }

  // Consumes from `void_at` and sets `at`.
  consume_void();

  return 0;
}


static inline int consume_basic_internal(struct token_t *t, int expect_value) {
  // expect_value:
  //    "{" is object
  //    "/" is regexp

  t->vp = td->void_at;
  t->p = td->at;
  t->line_no = td->line_no;
  t->special = 0;

  char startc = *(td->at);
  char is_symbol = lookup_symbol[startc];

  if (!startc) {
    t->len = 0;
    t->type = TOKEN_EOF;
    return 0;
  }

  if (is_symbol) {
    uint32_t special = 0;
    int lit_len = consume_known_lit(td->at, &special);
    char *at = td->at + lit_len;

    if (lookup_symbol[*at]) {
      special = 0;
      while (lookup_symbol[*at]) {
        // TODO: \ escapes
        ++at;
      }
    }

    t->len = at - td->at;
    t->special = special;
    t->type = 1;

    return 0;
  }

  switch (startc) {
    case '(':
      t->type = TOKEN_PAREN;
      t->len = 1;
      return 0;

    case '[':
      t->type = TOKEN_ARRAY;
      t->len = 1;
      return 0;

    case '{':
      t->type = TOKEN_BRACE;
      t->len = 1;
      return 0;

    case '/':
      if (expect_value) {
        return ERROR__TODO;
      }
      t->type = TOKEN_OP;
      t->len = 1;
      return 0;

    case ')':
    case '}':
    case ']':
      t->type = TOKEN_CLOSE;
      t->len = 1;
      return 0;
  }

  return ERROR__TODO;
}


static int consume_basic(struct token_t *t, int expect_value) {
  int err = consume_basic_internal(t, expect_value);
  if (err) {
    return err;
  }

  td->at += t->len;
  td->void_at = td->at;
  consume_void();
  return 0;
}



int gumnut_next() {
  td->buf_at++;
  memcpy(td->buf, td->buf + td->buf_at, sizeof(struct token_t));
  if (td->buf_at != td->buf_have) {
    // this was read earlier, return immediately
    return td->buf[0].type;
  }

  // otherwise we just copied the cursor in-place



  td->buf_at = 0;
  td->buf_have = 1;


  int err = consume_basic(first, 1);
  if (err) {
    return err;
  }

  struct token_t *curr = first;

  switch (first->special) {
    case LIT_FUNCTION:
      // nb. we only care if non-top-level (since / following is weird)

      if (cursor->type == TOKEN_OP && cursor->p[0] == '*' && cursor->len == 1) {
        // consume
      }

      if (cursor->type == TOKEN_LIT) {
        // consume
      }

      if (cursor->type != TOKEN_PAREN) {
        // fail
      }
      // consume and bump

      ++curr;
      consume_basic(curr, 0);
      if (curr->type == TYPE_LIT) {

      }

    case LIT_CLASS:
      // nb. we care (probably lots) but especially if top-level, since {} {}'s are weird
      
  }

  debugf("consumed %d", td->buf_have);

//  td->at = td->buf[td->buf_have - 1].at;

  return td->buf->type;
}


struct token_t *gumnut_cursor() {
  return td->buf;
}
