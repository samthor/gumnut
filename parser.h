#include "types.h"

#define CONTEXT__ASYNC 1  // pass at top for top-level-await
#define CONTEXT__NAKED 4  // naked expr, no ()'s

typedef void (*prsr_callback)(void *, token *);
int prsr_run(char *p, int context, prsr_callback, void *);
