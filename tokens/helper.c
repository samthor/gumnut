// Generated on Wed Apr 17 2019 11:23:22 GMT+1000 (Australian Eastern Standard Time)
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
        _done(5, 357217);  // async
      }
      _done(2, 160609);  // as
    case 119:  // 'w'
      if (*p++ != 97 || *p++ != 105 || *p++ != 116) {
        // != "ait"
        return p - start - 1;
      }
      _done(5, 358241);  // await
    }
    return 1;  // a...
  case 98:  // 'b'
    if (*p++ != 114 || *p++ != 101 || *p++ != 97 || *p++ != 107) {
      // != "reak"
      return p - start - 1;
    }
    _done(5, 356962);  // break
  case 99:  // 'c'
    switch (*p++) {
    case 97:  // 'a'
      switch (*p++) {
      case 115:  // 's'
        if (*p++ != 101) {
          // != "e"
          return 3;
        }
        _done(4, 287075);  // case
      case 116:  // 't'
        if (*p++ != 99 || *p++ != 104) {
          // != "ch"
          return p - start - 1;
        }
        _done(5, 352611);  // catch
      }
      return 2;  // ca...
    case 108:  // 'l'
      if (*p++ != 97 || *p++ != 115 || *p++ != 115) {
        // != "ass"
        return p - start - 1;
      }
      _done(5, 355427);  // class
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
        _done(5, 356195);  // const
      case 116:  // 't'
        if (*p++ != 105 || *p++ != 110 || *p++ != 117 || *p++ != 101) {
          // != "inue"
          return p - start - 1;
        }
        _done(8, 552803);  // continue
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
        _done(8, 550244);  // debugger
      case 102:  // 'f'
        if (*p++ != 97 || *p++ != 117 || *p++ != 108 || *p++ != 116) {
          // != "ault"
          return p - start - 1;
        }
        _done(7, 484708);  // default
      case 108:  // 'l'
        if (*p++ != 101 || *p++ != 116 || *p++ != 101) {
          // != "ete"
          return p - start - 1;
        }
        _done(6, 419172);  // delete
      }
      return 2;  // de...
    case 111:  // 'o'
      _done(2, 159588);  // do
    }
    return 1;  // d...
  case 101:  // 'e'
    switch (*p++) {
    case 108:  // 'l'
      if (*p++ != 115 || *p++ != 101) {
        // != "se"
        return p - start - 1;
      }
      _done(4, 289893);  // else
    case 110:  // 'n'
      if (*p++ != 117 || *p++ != 109) {
        // != "um"
        return p - start - 1;
      }
      _done(4, 290405);  // enum
    case 120:  // 'x'
      switch (*p++) {
      case 112:  // 'p'
        if (*p++ != 111 || *p++ != 114 || *p++ != 116) {
          // != "ort"
          return p - start - 1;
        }
        _done(6, 424037);  // export
      case 116:  // 't'
        if (*p++ != 101 || *p++ != 110 || *p++ != 100 || *p++ != 115) {
          // != "ends"
          return p - start - 1;
        }
        _done(7, 489573);  // extends
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
      _done(5, 352614);  // false
    case 105:  // 'i'
      if (*p++ != 110 || *p++ != 97 || *p++ != 108 || *p++ != 108 || *p++ != 121) {
        // != "nally"
        return p - start - 1;
      }
      _done(7, 485734);  // finally
    case 111:  // 'o'
      if (*p++ != 114) {
        // != "r"
        return 2;
      }
      _done(3, 225126);  // for
    case 114:  // 'r'
      if (*p++ != 111 || *p++ != 109) {
        // != "om"
        return p - start - 1;
      }
      _done(4, 291430);  // from
    case 117:  // 'u'
      if (*p++ != 110 || *p++ != 99 || *p++ != 116 || *p++ != 105 || *p++ != 111 || *p++ != 110) {
        // != "nction"
        return p - start - 1;
      }
      _done(8, 554342);  // function
    }
    return 1;  // f...
  case 105:  // 'i'
    switch (*p++) {
    case 102:  // 'f'
      _done(2, 157289);  // if
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
        _done(10, 683369);  // implements
      case 111:  // 'o'
        if (*p++ != 114 || *p++ != 116) {
          // != "rt"
          return p - start - 1;
        }
        _done(6, 421225);  // import
      }
      return 3;  // imp...
    case 110:  // 'n'
      switch (*p++) {
      case 115:  // 's'
        if (*p++ != 116 || *p++ != 97 || *p++ != 110 || *p++ != 99 || *p++ != 101 || *p++ != 111 || *p++ != 102) {
          // != "tanceof"
          return p - start - 1;
        }
        _done(10, 683625);  // instanceof
      case 116:  // 't'
        if (*p++ != 101 || *p++ != 114 || *p++ != 102 || *p++ != 97 || *p++ != 99 || *p++ != 101) {
          // != "erface"
          return p - start - 1;
        }
        _done(9, 618089);  // interface
      }
      _done(2, 159337);  // in
    }
    return 1;  // i...
  case 108:  // 'l'
    if (*p++ != 101 || *p++ != 116) {
      // != "et"
      return p - start - 1;
    }
    _done(3, 222572);  // let
  case 110:  // 'n'
    switch (*p++) {
    case 101:  // 'e'
      if (*p++ != 119) {
        // != "w"
        return 2;
      }
      _done(3, 222574);  // new
    case 117:  // 'u'
      if (*p++ != 108 || *p++ != 108) {
        // != "ll"
        return p - start - 1;
      }
      _done(4, 292206);  // null
    }
    return 1;  // n...
  case 112:  // 'p'
    switch (*p++) {
    case 97:  // 'a'
      if (*p++ != 99 || *p++ != 107 || *p++ != 97 || *p++ != 103 || *p++ != 101) {
        // != "ckage"
        return p - start - 1;
      }
      _done(7, 483696);  // package
    case 114:  // 'r'
      switch (*p++) {
      case 105:  // 'i'
        if (*p++ != 118 || *p++ != 97 || *p++ != 116 || *p++ != 101) {
          // != "vate"
          return p - start - 1;
        }
        _done(7, 488048);  // private
      case 111:  // 'o'
        if (*p++ != 116 || *p++ != 101 || *p++ != 99 || *p++ != 116 || *p++ != 101 || *p++ != 100) {
          // != "tected"
          return p - start - 1;
        }
        _done(9, 619120);  // protected
      }
      return 2;  // pr...
    case 117:  // 'u'
      if (*p++ != 98 || *p++ != 108 || *p++ != 105 || *p++ != 99) {
        // != "blic"
        return p - start - 1;
      }
      _done(6, 423280);  // public
    }
    return 1;  // p...
  case 114:  // 'r'
    if (*p++ != 101 || *p++ != 116 || *p++ != 117 || *p++ != 114 || *p++ != 110) {
      // != "eturn"
      return p - start - 1;
    }
    _done(6, 419186);  // return
  case 115:  // 's'
    switch (*p++) {
    case 116:  // 't'
      if (*p++ != 97 || *p++ != 116 || *p++ != 105 || *p++ != 99) {
        // != "atic"
        return p - start - 1;
      }
      _done(6, 423027);  // static
    case 117:  // 'u'
      if (*p++ != 112 || *p++ != 101 || *p++ != 114) {
        // != "per"
        return p - start - 1;
      }
      _done(5, 357747);  // super
    case 119:  // 'w'
      if (*p++ != 105 || *p++ != 116 || *p++ != 99 || *p++ != 104) {
        // != "itch"
        return p - start - 1;
      }
      _done(6, 423795);  // switch
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
        _done(4, 288884);  // this
      case 114:  // 'r'
        if (*p++ != 111 || *p++ != 119) {
          // != "ow"
          return p - start - 1;
        }
        _done(5, 354420);  // throw
      }
      return 2;  // th...
    case 114:  // 'r'
      switch (*p++) {
      case 117:  // 'u'
        if (*p++ != 101) {
          // != "e"
          return 3;
        }
        _done(4, 291444);  // true
      case 121:  // 'y'
        _done(3, 225908);  // try
      }
      return 2;  // tr...
    case 121:  // 'y'
      if (*p++ != 112 || *p++ != 101 || *p++ != 111 || *p++ != 102) {
        // != "peof"
        return p - start - 1;
      }
      _done(6, 424308);  // typeof
    }
    return 1;  // t...
  case 118:  // 'v'
    switch (*p++) {
    case 97:  // 'a'
      if (*p++ != 114) {
        // != "r"
        return 2;
      }
      _done(3, 221558);  // var
    case 111:  // 'o'
      if (*p++ != 105 || *p++ != 100) {
        // != "id"
        return p - start - 1;
      }
      _done(4, 290678);  // void
    }
    return 1;  // v...
  case 119:  // 'w'
    switch (*p++) {
    case 104:  // 'h'
      if (*p++ != 105 || *p++ != 108 || *p++ != 101) {
        // != "ile"
        return p - start - 1;
      }
      _done(5, 354423);  // while
    case 105:  // 'i'
      if (*p++ != 116 || *p++ != 104) {
        // != "th"
        return p - start - 1;
      }
      _done(4, 289143);  // with
    }
    return 1;  // w...
  case 121:  // 'y'
    if (*p++ != 105 || *p++ != 101 || *p++ != 108 || *p++ != 100) {
      // != "ield"
      return p - start - 1;
    }
    _done(5, 354681);  // yield
  }
  return 0;  // ...

#undef _done
}
