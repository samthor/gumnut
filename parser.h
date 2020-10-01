
#include "token.h"

int blep_parser_init(char *, int);
int blep_parser_run();
struct token *blep_parser_cursor();

// below must be provided

void blep_parser_callback();
int blep_parser_stack(int);

#define SPECIAL__DECLARE         2    // let, const, var
#define SPECIAL__TOP             4    // for "var" or var-like only (top-level)
#define SPECIAL__PROPERTY        8
#define SPECIAL__EXTERNAL        16   // is an external reference (import/export)
#define SPECIAL__CHANGE          32   // underlying symbol is being changed

#define SPECIAL__BASE            32768

#define STACK__EXPR       1
#define STACK__DECLARE    2  // var, let, const
#define STACK__CONTROL    3  // control start (if, for,... etc)
#define STACK__BLOCK      4  // {}'s
#define STACK__MODULE     5  // import or export
#define STACK__FUNCTION   6  // nb., defines new top-level scope
#define STACK__CLASS      7
#define STACK__MISC       8  // continue, break, return, throw, debugger
#define STACK__LABEL      9  // label "foo":


#define _STACK_MAX        9