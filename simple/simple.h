#include "../types.h"
#include "../token.h"

// context are set on all statements
#define CONTEXT__STRICT    1
#define CONTEXT__ASYNC     2
#define CONTEXT__GENERATOR 4

typedef void (*prsr_callback)(void *, token *);
int prsr_simple(tokendef *, int is_module, prsr_callback, void *);
