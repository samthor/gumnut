
#ifndef NULL
#define NULL ((char*)0)
#endif

#ifdef DEBUG
#include <stdio.h>
#define debugf(...) fprintf(stderr, "!!! " __VA_ARGS__); fprintf(stderr, "\n")
#else
#define debugf (void)sizeof
#endif
