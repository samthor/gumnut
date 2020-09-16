static const char *test = "{ \
  ; \
  foo: async function * foo (x, ...y) {break \n;break\n;}\n\
  123; \n\
  return\n\
  ; \n\
  async function bar() {} \n\
  // hello \n\
} 999";

#include "token.h"
#include "parser.h"
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

static int depth = 0;

void blep_parser_callback(struct token *t) {
  for (int i = 0; i < depth; ++i) {
    printf("  ");
  }

  printf("%.*s (%d)\n", t->len, t->p, t->type);
}

int blep_parser_stack(int type) {
  if (type) {
    ++depth;
    printf(">> %d\n", type);
  } else {
    --depth;
    printf("<<\n");
  }
  return 0;
}


#define BUFFER_SIZE  16
#define BUFFER_PEEK  8  // must have this many, must be <BUFFER_SIZE


struct buffer {
  int save;
  int restore_line_no;
  char *restore_at;
  int restore_depth;

  struct token pending[BUFFER_SIZE];
  int lookahead_length;

  struct token a, b;
  struct token *curr, *alt;
};

struct buffer _real_buffer;
struct buffer *buffer = &_real_buffer;


void blep_cursor_save() {
  if (++buffer->save == 1) {
    buffer->restore_line_no = td->line_no;
    buffer->restore_at = td->at;
    buffer->restore_depth = td->depth;
    buffer->lookahead_length = 0;
  }
}

struct token *blep_cursor_restore() {
  if (!buffer->save) {
    // TODO: should actually explode
    return buffer->curr;
  }

  if (--buffer->save) {
    if (buffer->lookahead_length) {
      return buffer->pending + buffer->lookahead_length - 1;
    }
    return buffer->curr;
  }

  // otherwise we restore at top position.

  td->line_no = buffer->restore_line_no;
  td->at = buffer->restore_at;
  td->depth = buffer->restore_depth;
  return buffer->curr;
}

struct token *blep_cursor_next() {
  if (buffer->alt->p) {
    struct token *tmp = buffer->alt;
    buffer->alt = buffer->curr;
    buffer->curr = tmp;
    buffer->alt->p = NULL;
    return tmp;
  }

  if (buffer->save) {
    struct token *prev = buffer->curr;
    struct token *curr = buffer->pending + buffer->lookahead_length;
    if (buffer->lookahead_length) {
      if (buffer->lookahead_length == BUFFER_SIZE) {
        // welp, out of space
        exit(1);
      }
      prev = buffer->pending + buffer->lookahead_length - 1;
    }

    blep_token_next(prev, curr);
    ++buffer->lookahead_length;

    if (td->depth < buffer->restore_depth) {
      // TODO: we should explode (could restore state, but...)
      exit(2);
    }

    return curr;
  }

  struct token *tmp = buffer->alt;
  buffer->alt = buffer->curr;
  buffer->curr = tmp;

  blep_token_next(buffer->alt, buffer->curr);
  buffer->alt->p = NULL;  // clear for potential peek

  return buffer->curr;
}

struct token *blep_cursor_peek() {
  // store in alt
  if (!buffer->alt->p) {
    blep_token_next(buffer->curr, buffer->alt);
  }
  return buffer->alt;
}



int main() {
  bzero(buffer, sizeof(struct buffer));

  buffer->curr = &(buffer->a);
  buffer->alt = &(buffer->b);

  int ret = blep_token_init((char *) test, strlen(test));
  if (ret) {
    return ret;
  }

  blep_parser_init();
  for (;;) {
    int ret = blep_parser_run();
    if (ret <= 0) {
      return ret;
    }
  }
}