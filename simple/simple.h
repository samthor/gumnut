#include "../types.h"
#include "../token.h"

typedef void (*prsr_callback)(void *, token *);
int prsr_simple(tokendef *, prsr_callback, void *);
