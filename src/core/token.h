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
int blep_token_next();


#define STACK_SIZE    256
#define RING_SIZE     8

// TODO: need to keep two tokens (peek), or one longer "run" of tokens

// RING_SIZE must fit "export default async function * foo (" (longest single part, 7)


typedef struct {
  int open;
  int block_has_value;
} tokendef_stack;



typedef struct {
  struct token curr;

  struct token buf[RING_SIZE];
  int buf_use;
  int buf_at;

  int line_no;  // line_no at head
  char *at;     // head pointer
  char *end;    // end of input (must point to NULL)

  // depth/stack at head (just used for balancing)
  int depth;
  tokendef_stack stack[STACK_SIZE];
} tokendef;

// global
#ifdef EMSCRIPTEN
#define td ((tokendef *) 20)
#else
extern tokendef _td;
#define td (&_td)
#endif

#endif//__BLEP_TOKEN_H
