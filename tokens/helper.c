// Generated on Wed Apr 17 2019 15:21:11 GMT+1000 (Australian Eastern Standard Time)

#include "lit.h"
#include "helper.h"

// 49 candidates:
//   as async await break case catch class const continue debugger default delete do else enum export extends false finally for from function if implements import in instanceof interface let new null package private protected public return static super switch this throw true try typeof var void while with yield
int consume_known_lit(char *p, uint32_t *out) {
  char *start = p;
#define _done(len, _out) {*out=_out;return len;}
  switch (*p++) {
  case 97:  // 'a'
    switch (*p++) {
    case 115:  // 's'
      switch (*p++) {
      case 121:  // 'y'
        if (*p++ != 110 || *p++ != 99) {
          // != "nc"
          return p - start - 1;
        }
        _done(5, LIT_ASYNC);
      }
      _done(2, LIT_AS);
    case 119:  // 'w'
      if (*p++ != 97 || *p++ != 105 || *p++ != 116) {
        // != "ait"
        return p - start - 1;
      }
      _done(5, LIT_AWAIT);
    }
    return 1;  // a...
  case 98:  // 'b'
    if (*p++ != 114 || *p++ != 101 || *p++ != 97 || *p++ != 107) {
      // != "reak"
      return p - start - 1;
    }
    _done(5, LIT_BREAK);
  case 99:  // 'c'
    switch (*p++) {
    case 97:  // 'a'
      switch (*p++) {
      case 115:  // 's'
        if (*p++ != 101) {
          // != "e"
          return 3;
        }
        _done(4, LIT_CASE);
      case 116:  // 't'
        if (*p++ != 99 || *p++ != 104) {
          // != "ch"
          return p - start - 1;
        }
        _done(5, LIT_CATCH);
      }
      return 2;  // ca...
    case 108:  // 'l'
      if (*p++ != 97 || *p++ != 115 || *p++ != 115) {
        // != "ass"
        return p - start - 1;
      }
      _done(5, LIT_CLASS);
    case 111:  // 'o'
      if (*p++ != 110) {
        // != "n"
        return 2;
      }
      switch (*p++) {
      case 115:  // 's'
        if (*p++ != 116) {
          // != "t"
          return 4;
        }
        _done(5, LIT_CONST);
      case 116:  // 't'
        if (*p++ != 105 || *p++ != 110 || *p++ != 117 || *p++ != 101) {
          // != "inue"
          return p - start - 1;
        }
        _done(8, LIT_CONTINUE);
      }
      return 3;  // con...
    }
    return 1;  // c...
  case 100:  // 'd'
    switch (*p++) {
    case 101:  // 'e'
      switch (*p++) {
      case 98:  // 'b'
        if (*p++ != 117 || *p++ != 103 || *p++ != 103 || *p++ != 101 || *p++ != 114) {
          // != "ugger"
          return p - start - 1;
        }
        _done(8, LIT_DEBUGGER);
      case 102:  // 'f'
        if (*p++ != 97 || *p++ != 117 || *p++ != 108 || *p++ != 116) {
          // != "ault"
          return p - start - 1;
        }
        _done(7, LIT_DEFAULT);
      case 108:  // 'l'
        if (*p++ != 101 || *p++ != 116 || *p++ != 101) {
          // != "ete"
          return p - start - 1;
        }
        _done(6, LIT_DELETE);
      }
      return 2;  // de...
    case 111:  // 'o'
      _done(2, LIT_DO);
    }
    return 1;  // d...
  case 101:  // 'e'
    switch (*p++) {
    case 108:  // 'l'
      if (*p++ != 115 || *p++ != 101) {
        // != "se"
        return p - start - 1;
      }
      _done(4, LIT_ELSE);
    case 110:  // 'n'
      if (*p++ != 117 || *p++ != 109) {
        // != "um"
        return p - start - 1;
      }
      _done(4, LIT_ENUM);
    case 120:  // 'x'
      switch (*p++) {
      case 112:  // 'p'
        if (*p++ != 111 || *p++ != 114 || *p++ != 116) {
          // != "ort"
          return p - start - 1;
        }
        _done(6, LIT_EXPORT);
      case 116:  // 't'
        if (*p++ != 101 || *p++ != 110 || *p++ != 100 || *p++ != 115) {
          // != "ends"
          return p - start - 1;
        }
        _done(7, LIT_EXTENDS);
      }
      return 2;  // ex...
    }
    return 1;  // e...
  case 102:  // 'f'
    switch (*p++) {
    case 97:  // 'a'
      if (*p++ != 108 || *p++ != 115 || *p++ != 101) {
        // != "lse"
        return p - start - 1;
      }
      _done(5, LIT_FALSE);
    case 105:  // 'i'
      if (*p++ != 110 || *p++ != 97 || *p++ != 108 || *p++ != 108 || *p++ != 121) {
        // != "nally"
        return p - start - 1;
      }
      _done(7, LIT_FINALLY);
    case 111:  // 'o'
      if (*p++ != 114) {
        // != "r"
        return 2;
      }
      _done(3, LIT_FOR);
    case 114:  // 'r'
      if (*p++ != 111 || *p++ != 109) {
        // != "om"
        return p - start - 1;
      }
      _done(4, LIT_FROM);
    case 117:  // 'u'
      if (*p++ != 110 || *p++ != 99 || *p++ != 116 || *p++ != 105 || *p++ != 111 || *p++ != 110) {
        // != "nction"
        return p - start - 1;
      }
      _done(8, LIT_FUNCTION);
    }
    return 1;  // f...
  case 105:  // 'i'
    switch (*p++) {
    case 102:  // 'f'
      _done(2, LIT_IF);
    case 109:  // 'm'
      if (*p++ != 112) {
        // != "p"
        return 2;
      }
      switch (*p++) {
      case 108:  // 'l'
        if (*p++ != 101 || *p++ != 109 || *p++ != 101 || *p++ != 110 || *p++ != 116 || *p++ != 115) {
          // != "ements"
          return p - start - 1;
        }
        _done(10, LIT_IMPLEMENTS);
      case 111:  // 'o'
        if (*p++ != 114 || *p++ != 116) {
          // != "rt"
          return p - start - 1;
        }
        _done(6, LIT_IMPORT);
      }
      return 3;  // imp...
    case 110:  // 'n'
      switch (*p++) {
      case 115:  // 's'
        if (*p++ != 116 || *p++ != 97 || *p++ != 110 || *p++ != 99 || *p++ != 101 || *p++ != 111 || *p++ != 102) {
          // != "tanceof"
          return p - start - 1;
        }
        _done(10, LIT_INSTANCEOF);
      case 116:  // 't'
        if (*p++ != 101 || *p++ != 114 || *p++ != 102 || *p++ != 97 || *p++ != 99 || *p++ != 101) {
          // != "erface"
          return p - start - 1;
        }
        _done(9, LIT_INTERFACE);
      }
      _done(2, LIT_IN);
    }
    return 1;  // i...
  case 108:  // 'l'
    if (*p++ != 101 || *p++ != 116) {
      // != "et"
      return p - start - 1;
    }
    _done(3, LIT_LET);
  case 110:  // 'n'
    switch (*p++) {
    case 101:  // 'e'
      if (*p++ != 119) {
        // != "w"
        return 2;
      }
      _done(3, LIT_NEW);
    case 117:  // 'u'
      if (*p++ != 108 || *p++ != 108) {
        // != "ll"
        return p - start - 1;
      }
      _done(4, LIT_NULL);
    }
    return 1;  // n...
  case 112:  // 'p'
    switch (*p++) {
    case 97:  // 'a'
      if (*p++ != 99 || *p++ != 107 || *p++ != 97 || *p++ != 103 || *p++ != 101) {
        // != "ckage"
        return p - start - 1;
      }
      _done(7, LIT_PACKAGE);
    case 114:  // 'r'
      switch (*p++) {
      case 105:  // 'i'
        if (*p++ != 118 || *p++ != 97 || *p++ != 116 || *p++ != 101) {
          // != "vate"
          return p - start - 1;
        }
        _done(7, LIT_PRIVATE);
      case 111:  // 'o'
        if (*p++ != 116 || *p++ != 101 || *p++ != 99 || *p++ != 116 || *p++ != 101 || *p++ != 100) {
          // != "tected"
          return p - start - 1;
        }
        _done(9, LIT_PROTECTED);
      }
      return 2;  // pr...
    case 117:  // 'u'
      if (*p++ != 98 || *p++ != 108 || *p++ != 105 || *p++ != 99) {
        // != "blic"
        return p - start - 1;
      }
      _done(6, LIT_PUBLIC);
    }
    return 1;  // p...
  case 114:  // 'r'
    if (*p++ != 101 || *p++ != 116 || *p++ != 117 || *p++ != 114 || *p++ != 110) {
      // != "eturn"
      return p - start - 1;
    }
    _done(6, LIT_RETURN);
  case 115:  // 's'
    switch (*p++) {
    case 116:  // 't'
      if (*p++ != 97 || *p++ != 116 || *p++ != 105 || *p++ != 99) {
        // != "atic"
        return p - start - 1;
      }
      _done(6, LIT_STATIC);
    case 117:  // 'u'
      if (*p++ != 112 || *p++ != 101 || *p++ != 114) {
        // != "per"
        return p - start - 1;
      }
      _done(5, LIT_SUPER);
    case 119:  // 'w'
      if (*p++ != 105 || *p++ != 116 || *p++ != 99 || *p++ != 104) {
        // != "itch"
        return p - start - 1;
      }
      _done(6, LIT_SWITCH);
    }
    return 1;  // s...
  case 116:  // 't'
    switch (*p++) {
    case 104:  // 'h'
      switch (*p++) {
      case 105:  // 'i'
        if (*p++ != 115) {
          // != "s"
          return 3;
        }
        _done(4, LIT_THIS);
      case 114:  // 'r'
        if (*p++ != 111 || *p++ != 119) {
          // != "ow"
          return p - start - 1;
        }
        _done(5, LIT_THROW);
      }
      return 2;  // th...
    case 114:  // 'r'
      switch (*p++) {
      case 117:  // 'u'
        if (*p++ != 101) {
          // != "e"
          return 3;
        }
        _done(4, LIT_TRUE);
      case 121:  // 'y'
        _done(3, LIT_TRY);
      }
      return 2;  // tr...
    case 121:  // 'y'
      if (*p++ != 112 || *p++ != 101 || *p++ != 111 || *p++ != 102) {
        // != "peof"
        return p - start - 1;
      }
      _done(6, LIT_TYPEOF);
    }
    return 1;  // t...
  case 118:  // 'v'
    switch (*p++) {
    case 97:  // 'a'
      if (*p++ != 114) {
        // != "r"
        return 2;
      }
      _done(3, LIT_VAR);
    case 111:  // 'o'
      if (*p++ != 105 || *p++ != 100) {
        // != "id"
        return p - start - 1;
      }
      _done(4, LIT_VOID);
    }
    return 1;  // v...
  case 119:  // 'w'
    switch (*p++) {
    case 104:  // 'h'
      if (*p++ != 105 || *p++ != 108 || *p++ != 101) {
        // != "ile"
        return p - start - 1;
      }
      _done(5, LIT_WHILE);
    case 105:  // 'i'
      if (*p++ != 116 || *p++ != 104) {
        // != "th"
        return p - start - 1;
      }
      _done(4, LIT_WITH);
    }
    return 1;  // w...
  case 121:  // 'y'
    if (*p++ != 105 || *p++ != 101 || *p++ != 108 || *p++ != 100) {
      // != "ield"
      return p - start - 1;
    }
    _done(5, LIT_YIELD);
  }
  return 0;  // ...

#undef _done
}
