#include <stdint.h>

#ifndef __BLEP_TOKEN_H
#define __BLEP_TOKEN_H

#include "def.h"

struct token {
  char *vp;  // void-pointer (before token)
  char *p;
  int len;
  int line_no;
  int type;
  uint32_t special;
};


int blep_token_init(char *, int);
int blep_token_update(int);
int blep_token_next();
int blep_token_peek();

int blep_token_set_restore();
int blep_token_restore();

inline int blep_token_is_symbol_part(char);


#define STACK_SIZE    256



typedef struct {
  struct token curr;  // cursor before head
  struct token peek;  // also before head if p is !NULL

  int line_no;  // line_no at head
  char *at;     // head pointer
  char *end;    // end of input (must point to NULL)

  // depth/stack at head (just used for balancing)
  int depth;
  int stack[STACK_SIZE];

  struct token restore__curr;
  int restore__line_no;
  char *restore__at;
  int restore__depth;
} tokendef;

// global
#ifdef EMSCRIPTEN
#define td ((tokendef *) 20)
#else
extern tokendef _td;
#define td (&_td)
#endif

#endif//__BLEP_TOKEN_H
