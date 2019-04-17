// Generated on Wed Apr 17 2019 14:11:36 GMT+1000 (Australian Eastern Standard Time)
// 65 candidates:
//   async break case catch class const continue debugger default delete do else enum export extends finally for function if import new return static switch throw try typeof var void while with implements package protected interface private public false in instanceof null super this true as await from let yield abstract boolean byte char double final float goto int long native short synchronized throws transient volatile
int consume_known_lit(char *p, uint32_t *out) {
  char *start = p;
#define _done(len, _out) {*out=_out;return len;}
  switch (*p++) {
  case 97:  // 'a'
    switch (*p++) {
    case 98:  // 'b'
      if (*p++ != 115 || *p++ != 116 || *p++ != 114 || *p++ != 97 || *p++ != 99 || *p++ != 116) {
        // != "stract"
        return p - start - 1;
      }
      _done(8, 549473);  // abstract
    case 115:  // 's'
      switch (*p++) {
      case 121:  // 'y'
        if (*p++ != 110 || *p++ != 99) {
          // != "nc"
          return p - start - 1;
        }
        _done(5, 17134433);  // async
      }
      _done(2, 134378337);  // as
    case 119:  // 'w'
      if (*p++ != 97 || *p++ != 105 || *p++ != 116) {
        // != "ait"
        return p - start - 1;
      }
      _done(5, 134575969);  // await
    }
    return 1;  // a...
  case 98:  // 'b'
    switch (*p++) {
    case 111:  // 'o'
      if (*p++ != 111 || *p++ != 108 || *p++ != 101 || *p++ != 97 || *p++ != 110) {
        // != "olean"
        return p - start - 1;
      }
      _done(7, 487266);  // boolean
    case 114:  // 'r'
      if (*p++ != 101 || *p++ != 97 || *p++ != 107) {
        // != "eak"
        return p - start - 1;
      }
      _done(5, 17134178);  // break
    case 121:  // 'y'
      if (*p++ != 116 || *p++ != 101) {
        // != "te"
        return p - start - 1;
      }
      _done(4, 293218);  // byte
    }
    return 1;  // b...
  case 99:  // 'c'
    switch (*p++) {
    case 97:  // 'a'
      switch (*p++) {
      case 115:  // 's'
        if (*p++ != 101) {
          // != "e"
          return 3;
        }
        _done(4, 17064291);  // case
      case 116:  // 't'
        if (*p++ != 99 || *p++ != 104) {
          // != "ch"
          return p - start - 1;
        }
        _done(5, 17129827);  // catch
      }
      return 2;  // ca...
    case 104:  // 'h'
      if (*p++ != 97 || *p++ != 114) {
        // != "ar"
        return p - start - 1;
      }
      _done(4, 288867);  // char
    case 108:  // 'l'
      if (*p++ != 97 || *p++ != 115 || *p++ != 115) {
        // != "ass"
        return p - start - 1;
      }
      _done(5, 17132643);  // class
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
        _done(5, 17133411);  // const
      case 116:  // 't'
        if (*p++ != 105 || *p++ != 110 || *p++ != 117 || *p++ != 101) {
          // != "inue"
          return p - start - 1;
        }
        _done(8, 17330019);  // continue
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
        _done(8, 17327460);  // debugger
      case 102:  // 'f'
        if (*p++ != 97 || *p++ != 117 || *p++ != 108 || *p++ != 116) {
          // != "ault"
          return p - start - 1;
        }
        _done(7, 17261924);  // default
      case 108:  // 'l'
        if (*p++ != 101 || *p++ != 116 || *p++ != 101) {
          // != "ete"
          return p - start - 1;
        }
        _done(6, 17196388);  // delete
      }
      return 2;  // de...
    case 111:  // 'o'
      switch (*p++) {
      case 117:  // 'u'
        if (*p++ != 98 || *p++ != 108 || *p++ != 101) {
          // != "ble"
          return p - start - 1;
        }
        _done(6, 421732);  // double
      }
      _done(2, 16936804);  // do
    }
    return 1;  // d...
  case 101:  // 'e'
    switch (*p++) {
    case 108:  // 'l'
      if (*p++ != 115 || *p++ != 101) {
        // != "se"
        return p - start - 1;
      }
      _done(4, 17067109);  // else
    case 110:  // 'n'
      if (*p++ != 117 || *p++ != 109) {
        // != "um"
        return p - start - 1;
      }
      _done(4, 17067621);  // enum
    case 120:  // 'x'
      switch (*p++) {
      case 112:  // 'p'
        if (*p++ != 111 || *p++ != 114 || *p++ != 116) {
          // != "ort"
          return p - start - 1;
        }
        _done(6, 17201253);  // export
      case 116:  // 't'
        if (*p++ != 101 || *p++ != 110 || *p++ != 100 || *p++ != 115) {
          // != "ends"
          return p - start - 1;
        }
        _done(7, 17266789);  // extends
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
      _done(5, 67461478);  // false
    case 105:  // 'i'
      if (*p++ != 110 || *p++ != 97 || *p++ != 108) {
        // != "nal"
        return p - start - 1;
      }
      switch (*p++) {
      case 108:  // 'l'
        if (*p++ != 121) {
          // != "y"
          return 6;
        }
        _done(7, 17262950);  // finally
      }
      _done(5, 354662);  // final
    case 108:  // 'l'
      if (*p++ != 111 || *p++ != 97 || *p++ != 116) {
        // != "oat"
        return p - start - 1;
      }
      _done(5, 355430);  // float
    case 111:  // 'o'
      if (*p++ != 114) {
        // != "r"
        return 2;
      }
      _done(3, 17002342);  // for
    case 114:  // 'r'
      if (*p++ != 111 || *p++ != 109) {
        // != "om"
        return p - start - 1;
      }
      _done(4, 134509158);  // from
    case 117:  // 'u'
      if (*p++ != 110 || *p++ != 99 || *p++ != 116 || *p++ != 105 || *p++ != 111 || *p++ != 110) {
        // != "nction"
        return p - start - 1;
      }
      _done(8, 17331558);  // function
    }
    return 1;  // f...
  case 103:  // 'g'
    if (*p++ != 111 || *p++ != 116 || *p++ != 111) {
      // != "oto"
      return p - start - 1;
    }
    _done(4, 290663);  // goto
  case 105:  // 'i'
    switch (*p++) {
    case 102:  // 'f'
      _done(2, 16934505);  // if
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
        _done(10, 34237801);  // implements
      case 111:  // 'o'
        if (*p++ != 114 || *p++ != 116) {
          // != "rt"
          return p - start - 1;
        }
        _done(6, 17198441);  // import
      }
      return 3;  // imp...
    case 110:  // 'n'
      switch (*p++) {
      case 115:  // 's'
        if (*p++ != 116 || *p++ != 97 || *p++ != 110 || *p++ != 99 || *p++ != 101 || *p++ != 111 || *p++ != 102) {
          // != "tanceof"
          return p - start - 1;
        }
        _done(10, 67792489);  // instanceof
      case 116:  // 't'
        switch (*p++) {
        case 101:  // 'e'
          if (*p++ != 114 || *p++ != 102 || *p++ != 97 || *p++ != 99 || *p++ != 101) {
            // != "rface"
            return p - start - 1;
          }
          _done(9, 34172521);  // interface
        }
        _done(3, 224873);  // int
      }
      _done(2, 67268201);  // in
    }
    return 1;  // i...
  case 108:  // 'l'
    switch (*p++) {
    case 101:  // 'e'
      if (*p++ != 116) {
        // != "t"
        return 2;
      }
      _done(3, 134440300);  // let
    case 111:  // 'o'
      if (*p++ != 110 || *p++ != 103) {
        // != "ng"
        return p - start - 1;
      }
      _done(4, 290668);  // long
    }
    return 1;  // l...
  case 110:  // 'n'
    switch (*p++) {
    case 97:  // 'a'
      if (*p++ != 116 || *p++ != 105 || *p++ != 118 || *p++ != 101) {
        // != "tive"
        return p - start - 1;
      }
      _done(6, 418158);  // native
    case 101:  // 'e'
      if (*p++ != 119) {
        // != "w"
        return 2;
      }
      _done(3, 16999790);  // new
    case 117:  // 'u'
      if (*p++ != 108 || *p++ != 108) {
        // != "ll"
        return p - start - 1;
      }
      _done(4, 67401070);  // null
    }
    return 1;  // n...
  case 112:  // 'p'
    switch (*p++) {
    case 97:  // 'a'
      if (*p++ != 99 || *p++ != 107 || *p++ != 97 || *p++ != 103 || *p++ != 101) {
        // != "ckage"
        return p - start - 1;
      }
      _done(7, 34038128);  // package
    case 114:  // 'r'
      switch (*p++) {
      case 105:  // 'i'
        if (*p++ != 118 || *p++ != 97 || *p++ != 116 || *p++ != 101) {
          // != "vate"
          return p - start - 1;
        }
        _done(7, 34042480);  // private
      case 111:  // 'o'
        if (*p++ != 116 || *p++ != 101 || *p++ != 99 || *p++ != 116 || *p++ != 101 || *p++ != 100) {
          // != "tected"
          return p - start - 1;
        }
        _done(9, 34173552);  // protected
      }
      return 2;  // pr...
    case 117:  // 'u'
      if (*p++ != 98 || *p++ != 108 || *p++ != 105 || *p++ != 99) {
        // != "blic"
        return p - start - 1;
      }
      _done(6, 33977712);  // public
    }
    return 1;  // p...
  case 114:  // 'r'
    if (*p++ != 101 || *p++ != 116 || *p++ != 117 || *p++ != 114 || *p++ != 110) {
      // != "eturn"
      return p - start - 1;
    }
    _done(6, 17196402);  // return
  case 115:  // 's'
    switch (*p++) {
    case 104:  // 'h'
      if (*p++ != 111 || *p++ != 114 || *p++ != 116) {
        // != "ort"
        return p - start - 1;
      }
      _done(5, 354419);  // short
    case 116:  // 't'
      if (*p++ != 97 || *p++ != 116 || *p++ != 105 || *p++ != 99) {
        // != "atic"
        return p - start - 1;
      }
      _done(6, 17200243);  // static
    case 117:  // 'u'
      if (*p++ != 112 || *p++ != 101 || *p++ != 114) {
        // != "per"
        return p - start - 1;
      }
      _done(5, 67466611);  // super
    case 119:  // 'w'
      if (*p++ != 105 || *p++ != 116 || *p++ != 99 || *p++ != 104) {
        // != "itch"
        return p - start - 1;
      }
      _done(6, 17201011);  // switch
    case 121:  // 'y'
      if (*p++ != 110 || *p++ != 99 || *p++ != 104 || *p++ != 114 || *p++ != 111 || *p++ != 110 || *p++ != 105 || *p++ != 122 || *p++ != 101 || *p++ != 100) {
        // != "nchronized"
        return p - start - 1;
      }
      _done(12, 817523);  // synchronized
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
        _done(4, 67397748);  // this
      case 114:  // 'r'
        if (*p++ != 111 || *p++ != 119) {
          // != "ow"
          return p - start - 1;
        }
        switch (*p++) {
        case 115:  // 's'
          _done(6, 419956);  // throws
        }
        _done(5, 17131636);  // throw
      }
      return 2;  // th...
    case 114:  // 'r'
      switch (*p++) {
      case 97:  // 'a'
        if (*p++ != 110 || *p++ != 115 || *p++ != 105 || *p++ != 101 || *p++ != 110 || *p++ != 116) {
          // != "nsient"
          return p - start - 1;
        }
        _done(9, 619124);  // transient
      case 117:  // 'u'
        if (*p++ != 101) {
          // != "e"
          return 3;
        }
        _done(4, 67400308);  // true
      case 121:  // 'y'
        _done(3, 17003124);  // try
      }
      return 2;  // tr...
    case 121:  // 'y'
      if (*p++ != 112 || *p++ != 101 || *p++ != 111 || *p++ != 102) {
        // != "peof"
        return p - start - 1;
      }
      _done(6, 17201524);  // typeof
    }
    return 1;  // t...
  case 118:  // 'v'
    switch (*p++) {
    case 97:  // 'a'
      if (*p++ != 114) {
        // != "r"
        return 2;
      }
      _done(3, 16998774);  // var
    case 111:  // 'o'
      switch (*p++) {
      case 105:  // 'i'
        if (*p++ != 100) {
          // != "d"
          return 3;
        }
        _done(4, 17067894);  // void
      case 108:  // 'l'
        if (*p++ != 97 || *p++ != 116 || *p++ != 105 || *p++ != 108 || *p++ != 101) {
          // != "atile"
          return p - start - 1;
        }
        _done(8, 552822);  // volatile
      }
      return 2;  // vo...
    }
    return 1;  // v...
  case 119:  // 'w'
    switch (*p++) {
    case 104:  // 'h'
      if (*p++ != 105 || *p++ != 108 || *p++ != 101) {
        // != "ile"
        return p - start - 1;
      }
      _done(5, 17131639);  // while
    case 105:  // 'i'
      if (*p++ != 116 || *p++ != 104) {
        // != "th"
        return p - start - 1;
      }
      _done(4, 17066359);  // with
    }
    return 1;  // w...
  case 121:  // 'y'
    if (*p++ != 105 || *p++ != 101 || *p++ != 108 || *p++ != 100) {
      // != "ield"
      return p - start - 1;
    }
    _done(5, 134572409);  // yield
  }
  return 0;  // ...

#undef _done
}
