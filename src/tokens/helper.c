// Generated on Tue May 26 2020 23:01:50 GMT+1000 (Australian Eastern Standard Time)

#include "lit.h"
#include "helper.h"

// 53 candidates:
//   as async await break case catch class const continue debugger default delete do else enum export extends false finally for from function get if implements import in instanceof interface let new null of package private protected public return set static super switch this throw true try typeof undefined var void while with yield
int consume_known_lit(char *p, uint32_t *out) {
  char *start = p;
#define _done(len, _out) {*out=_out;return len;}
  switch (*p++) {
  case 'a':
    switch (*p++) {
    case 's':
      switch (*p++) {
      case 'y':
        if (*p++ != 'n' || *p++ != 'c') {
          return p - start - 1;
        }
        _done(5, LIT_ASYNC);
      }
      _done(2, LIT_AS);
    case 'w':
      if (*p++ != 'a' || *p++ != 'i' || *p++ != 't') {
        return p - start - 1;
      }
      _done(5, LIT_AWAIT);
    }
    return 1;  // a...
  case 'b':
    if (*p++ != 'r' || *p++ != 'e' || *p++ != 'a' || *p++ != 'k') {
      return p - start - 1;
    }
    _done(5, LIT_BREAK);
  case 'c':
    switch (*p++) {
    case 'a':
      switch (*p++) {
      case 's':
        if (*p++ != 'e') {
          return 3;
        }
        _done(4, LIT_CASE);
      case 't':
        if (*p++ != 'c' || *p++ != 'h') {
          return p - start - 1;
        }
        _done(5, LIT_CATCH);
      }
      return 2;  // ca...
    case 'l':
      if (*p++ != 'a' || *p++ != 's' || *p++ != 's') {
        return p - start - 1;
      }
      _done(5, LIT_CLASS);
    case 'o':
      if (*p++ != 'n') {
        return 2;
      }
      switch (*p++) {
      case 's':
        if (*p++ != 't') {
          return 4;
        }
        _done(5, LIT_CONST);
      case 't':
        if (*p++ != 'i' || *p++ != 'n' || *p++ != 'u' || *p++ != 'e') {
          return p - start - 1;
        }
        _done(8, LIT_CONTINUE);
      }
      return 3;  // con...
    }
    return 1;  // c...
  case 'd':
    switch (*p++) {
    case 'e':
      switch (*p++) {
      case 'b':
        if (*p++ != 'u' || *p++ != 'g' || *p++ != 'g' || *p++ != 'e' || *p++ != 'r') {
          return p - start - 1;
        }
        _done(8, LIT_DEBUGGER);
      case 'f':
        if (*p++ != 'a' || *p++ != 'u' || *p++ != 'l' || *p++ != 't') {
          return p - start - 1;
        }
        _done(7, LIT_DEFAULT);
      case 'l':
        if (*p++ != 'e' || *p++ != 't' || *p++ != 'e') {
          return p - start - 1;
        }
        _done(6, LIT_DELETE);
      }
      return 2;  // de...
    case 'o':
      _done(2, LIT_DO);
    }
    return 1;  // d...
  case 'e':
    switch (*p++) {
    case 'l':
      if (*p++ != 's' || *p++ != 'e') {
        return p - start - 1;
      }
      _done(4, LIT_ELSE);
    case 'n':
      if (*p++ != 'u' || *p++ != 'm') {
        return p - start - 1;
      }
      _done(4, LIT_ENUM);
    case 'x':
      switch (*p++) {
      case 'p':
        if (*p++ != 'o' || *p++ != 'r' || *p++ != 't') {
          return p - start - 1;
        }
        _done(6, LIT_EXPORT);
      case 't':
        if (*p++ != 'e' || *p++ != 'n' || *p++ != 'd' || *p++ != 's') {
          return p - start - 1;
        }
        _done(7, LIT_EXTENDS);
      }
      return 2;  // ex...
    }
    return 1;  // e...
  case 'f':
    switch (*p++) {
    case 'a':
      if (*p++ != 'l' || *p++ != 's' || *p++ != 'e') {
        return p - start - 1;
      }
      _done(5, LIT_FALSE);
    case 'i':
      if (*p++ != 'n' || *p++ != 'a' || *p++ != 'l' || *p++ != 'l' || *p++ != 'y') {
        return p - start - 1;
      }
      _done(7, LIT_FINALLY);
    case 'o':
      if (*p++ != 'r') {
        return 2;
      }
      _done(3, LIT_FOR);
    case 'r':
      if (*p++ != 'o' || *p++ != 'm') {
        return p - start - 1;
      }
      _done(4, LIT_FROM);
    case 'u':
      if (*p++ != 'n' || *p++ != 'c' || *p++ != 't' || *p++ != 'i' || *p++ != 'o' || *p++ != 'n') {
        return p - start - 1;
      }
      _done(8, LIT_FUNCTION);
    }
    return 1;  // f...
  case 'g':
    if (*p++ != 'e' || *p++ != 't') {
      return p - start - 1;
    }
    _done(3, LIT_GET);
  case 'i':
    switch (*p++) {
    case 'f':
      _done(2, LIT_IF);
    case 'm':
      if (*p++ != 'p') {
        return 2;
      }
      switch (*p++) {
      case 'l':
        if (*p++ != 'e' || *p++ != 'm' || *p++ != 'e' || *p++ != 'n' || *p++ != 't' || *p++ != 's') {
          return p - start - 1;
        }
        _done(10, LIT_IMPLEMENTS);
      case 'o':
        if (*p++ != 'r' || *p++ != 't') {
          return p - start - 1;
        }
        _done(6, LIT_IMPORT);
      }
      return 3;  // imp...
    case 'n':
      switch (*p++) {
      case 's':
        if (*p++ != 't' || *p++ != 'a' || *p++ != 'n' || *p++ != 'c' || *p++ != 'e' || *p++ != 'o' || *p++ != 'f') {
          return p - start - 1;
        }
        _done(10, LIT_INSTANCEOF);
      case 't':
        if (*p++ != 'e' || *p++ != 'r' || *p++ != 'f' || *p++ != 'a' || *p++ != 'c' || *p++ != 'e') {
          return p - start - 1;
        }
        _done(9, LIT_INTERFACE);
      }
      _done(2, LIT_IN);
    }
    return 1;  // i...
  case 'l':
    if (*p++ != 'e' || *p++ != 't') {
      return p - start - 1;
    }
    _done(3, LIT_LET);
  case 'n':
    switch (*p++) {
    case 'e':
      if (*p++ != 'w') {
        return 2;
      }
      _done(3, LIT_NEW);
    case 'u':
      if (*p++ != 'l' || *p++ != 'l') {
        return p - start - 1;
      }
      _done(4, LIT_NULL);
    }
    return 1;  // n...
  case 'o':
    if (*p++ != 'f') {
      return 1;
    }
    _done(2, LIT_OF);
  case 'p':
    switch (*p++) {
    case 'a':
      if (*p++ != 'c' || *p++ != 'k' || *p++ != 'a' || *p++ != 'g' || *p++ != 'e') {
        return p - start - 1;
      }
      _done(7, LIT_PACKAGE);
    case 'r':
      switch (*p++) {
      case 'i':
        if (*p++ != 'v' || *p++ != 'a' || *p++ != 't' || *p++ != 'e') {
          return p - start - 1;
        }
        _done(7, LIT_PRIVATE);
      case 'o':
        if (*p++ != 't' || *p++ != 'e' || *p++ != 'c' || *p++ != 't' || *p++ != 'e' || *p++ != 'd') {
          return p - start - 1;
        }
        _done(9, LIT_PROTECTED);
      }
      return 2;  // pr...
    case 'u':
      if (*p++ != 'b' || *p++ != 'l' || *p++ != 'i' || *p++ != 'c') {
        return p - start - 1;
      }
      _done(6, LIT_PUBLIC);
    }
    return 1;  // p...
  case 'r':
    if (*p++ != 'e' || *p++ != 't' || *p++ != 'u' || *p++ != 'r' || *p++ != 'n') {
      return p - start - 1;
    }
    _done(6, LIT_RETURN);
  case 's':
    switch (*p++) {
    case 'e':
      if (*p++ != 't') {
        return 2;
      }
      _done(3, LIT_SET);
    case 't':
      if (*p++ != 'a' || *p++ != 't' || *p++ != 'i' || *p++ != 'c') {
        return p - start - 1;
      }
      _done(6, LIT_STATIC);
    case 'u':
      if (*p++ != 'p' || *p++ != 'e' || *p++ != 'r') {
        return p - start - 1;
      }
      _done(5, LIT_SUPER);
    case 'w':
      if (*p++ != 'i' || *p++ != 't' || *p++ != 'c' || *p++ != 'h') {
        return p - start - 1;
      }
      _done(6, LIT_SWITCH);
    }
    return 1;  // s...
  case 't':
    switch (*p++) {
    case 'h':
      switch (*p++) {
      case 'i':
        if (*p++ != 's') {
          return 3;
        }
        _done(4, LIT_THIS);
      case 'r':
        if (*p++ != 'o' || *p++ != 'w') {
          return p - start - 1;
        }
        _done(5, LIT_THROW);
      }
      return 2;  // th...
    case 'r':
      switch (*p++) {
      case 'u':
        if (*p++ != 'e') {
          return 3;
        }
        _done(4, LIT_TRUE);
      case 'y':
        _done(3, LIT_TRY);
      }
      return 2;  // tr...
    case 'y':
      if (*p++ != 'p' || *p++ != 'e' || *p++ != 'o' || *p++ != 'f') {
        return p - start - 1;
      }
      _done(6, LIT_TYPEOF);
    }
    return 1;  // t...
  case 'u':
    if (*p++ != 'n' || *p++ != 'd' || *p++ != 'e' || *p++ != 'f' || *p++ != 'i' || *p++ != 'n' || *p++ != 'e' || *p++ != 'd') {
      return p - start - 1;
    }
    _done(9, LIT_UNDEFINED);
  case 'v':
    switch (*p++) {
    case 'a':
      if (*p++ != 'r') {
        return 2;
      }
      _done(3, LIT_VAR);
    case 'o':
      if (*p++ != 'i' || *p++ != 'd') {
        return p - start - 1;
      }
      _done(4, LIT_VOID);
    }
    return 1;  // v...
  case 'w':
    switch (*p++) {
    case 'h':
      if (*p++ != 'i' || *p++ != 'l' || *p++ != 'e') {
        return p - start - 1;
      }
      _done(5, LIT_WHILE);
    case 'i':
      if (*p++ != 't' || *p++ != 'h') {
        return p - start - 1;
      }
      _done(4, LIT_WITH);
    }
    return 1;  // w...
  case 'y':
    if (*p++ != 'i' || *p++ != 'e' || *p++ != 'l' || *p++ != 'd') {
      return p - start - 1;
    }
    _done(5, LIT_YIELD);
  }
  return 0;  // ...

#undef _done
}
