
#ifndef __GUMNUT_TOKEN_H
#define __GUMNUT_TOKEN_H

#include <unistd.h>

struct token_t {
  char *vp;  // void-pointer (before token, includes comment)
  char *p;   // pointer to token
  int line_no;
  int len;   // length
  int type;
  uint32_t special;
};

struct gumnut_internal_t {
  struct token_t buf[8];
  int buf_have;
  int buf_at;

  char *void_at;
  int line_no;  // line_no at head
  char *at;     // head pointer
  char *end;    // end of input (must point to NULL)

  int is_rest;  // is at rest/top-level of code
};

int gumnut_init(char *, int);
int gumnut_next();
struct token_t *gumnut_cursor();

#endif/*__GUMNUT_TOKEN_H*/
