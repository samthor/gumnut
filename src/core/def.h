
#ifndef __BLEP_DEF_H
#define __BLEP_DEF_H


#define ERROR__UNEXPECTED -1
#define ERROR__STACK      -2  // stack didn't balance
#define ERROR__INTERNAL   -3  // internal error
#define ERROR__TODO       -4


#define TOKEN_EOF       0
#define TOKEN_LIT       1   // named thing
#define TOKEN_SEMICOLON 2   // ;
#define TOKEN_OP        3   // includes 'in', 'instanceof', 'of', 'void'
#define TOKEN_COLON     4   // used in label or dict
#define TOKEN_BRACE     5   // {
#define TOKEN_ARRAY     6   // [
#define TOKEN_PAREN     7   // (
#define TOKEN_TERNARY   8   // starts ternary block, "? ... :"
#define TOKEN_CLOSE     9   // '}', ']', ')', or ternary ':'
#define TOKEN_STRING    10
#define TOKEN_REGEXP    11  // literal "/foo/g", not "new RegExp('foo')"
#define TOKEN_NUMBER    12
#define TOKEN_SYMBOL    13  // this and above must be literal types
#define TOKEN_KEYWORD   14
#define TOKEN_LABEL     15  // to the left of a ':', e.g. 'foo:'
#define _TOKEN_MAX      15


#define SPECIAL__NEWLINE         1
#define SPECIAL__DECLARE         2    // let, const, var
#define SPECIAL__TOP             4    // for "var" or var-like only (top-level)
#define SPECIAL__PROPERTY        8
#define SPECIAL__CHANGE          16   // underlying symbol is being changed
#define SPECIAL__EXTERNAL        32   // is an external reference (import/export)
#define SPECIAL__LIT             (1 << 30)


#define STACK__EXPR       1   // top-level statement expr
#define STACK__DECLARE    2   // var, let, const
#define STACK__CONTROL    3   // control start (if, for,... etc)
#define STACK__BLOCK      4   // {}'s
#define STACK__FUNCTION   5   // function including arrowfunc (defines scope)
#define STACK__CLASS      6   // any class
#define STACK__MISC       7   // continue, break, return, throw, debugger
#define STACK__LABEL      8   // label "foo":
#define STACK__EXPORT     9   // normal export
#define STACK__MODULE     10  // import/export that interacts with another
#define STACK__INNER      11  // inner of function/class, defines top-level scope
#define _STACK_MAX        11


#endif//__BLEP_DEF_H
