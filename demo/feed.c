
#include <string.h>
#include "feed.h"
#include "../utils.h"

#define _PAREN_FOR   1
#define _PAREN_CATCH 2
#define _PAREN_DEF   3

#define _ret(x) {int _c = (x); if (_c) { return _c; }}

typedef struct {
  tokendef *td;
  prsr_callback cb;
  void *arg;

  token last, curr;
  token *next;
} feeddef;

static int state__slist(feeddef *fd);
static int state__s(feeddef *fd);

static int is_decl_start(token *curr, token *next) {
  if (curr->type != TOKEN_LIT || !is_decl_keyword(curr->p, curr->len)) {
    return 0;
  }

  if (curr->p[0] != 'l' || next->type == TOKEN_BRACE || next->type == TOKEN_ARRAY) {
    // const, var or e.g. "let[..]" or "let{..}", destructuring
  } else if (next->type != TOKEN_LIT || is_op_keyword(next->p, next->len)) {
    return 0;  // no following literal (e.g. "let = 1", "instanceof" counts as op)
  }

  return 1;
}

static int token_string(token *t, char *s, int len) {
  return t->len == len && !memcmp(t->p, s, len);
}

static int _read(feeddef *fd, int has_value) {
  if (fd->curr.type) {
    // this was the last thing worked on, emit it
    fd->cb(fd->arg, &(fd->curr));
  }

  fd->last = fd->curr;

  for (;;) {
    int out = prsr_next_token(fd->td, &(fd->curr), has_value);
    if (out || fd->curr.type != TOKEN_COMMENT) {
      return out;
    }

    // emit all comments
    fd->cb(fd->arg, &(fd->curr));
  }
}

static int _read_asi(feeddef *fd) {
  if (fd->curr.type) {
    // this was the last thing worked on, emit it
    fd->cb(fd->arg, &(fd->curr));
  }

  fd->last = fd->curr;

  bzero(&(fd->curr), sizeof(token));
  fd->curr.type = TOKEN_SEMICOLON;
  fd->curr.line_no = fd->last.line_no;

  return 0;
}

static int state__value(feeddef *fd, int is_next) {
restart_value:
  if (is_next) {
    _ret(_read(fd, 0));
  }

  // a value is loosely a series of formulas separated by commas
  for (;;) {
    // match early qualifiers +, -, await, delete, new, typeof, void, yield
    // FIXME: 'yield'/'await' placement rules
    // FIXME: "new" is tightly coupled to a single rvalue

    // Match any number of +, -, await, delete, typeof, void, yield
    for (;;) {
      if (fd->curr.type == TOKEN_OP) {
        if (fd->curr.len == 1 && (fd->curr.p[0] == '-' || fd->curr.p[0] == '+')) {
          // ok
        } else {
          break;
        }
      } else if (fd->curr.type == TOKEN_LIT) {
        if (!is_expr_keyword(fd->curr.p, fd->curr.len) || fd->curr.p[0] == 'n') {
          break;
        }
        fd->curr.type = TOKEN_KEYWORD;
      } else {
        // TODO: is this right? no match - return?
        return 0;
      }
      _ret(_read(fd, 0));  // consume
    }

    // "new" can only appear right before a value, and only once
    if (fd->curr.type == TOKEN_LIT && token_string(&(fd->curr), "new", 3)) {
      fd->curr.type = TOKEN_KEYWORD;
      _read(fd, 0);
    }

    // allow prefix ++ or --
    if (fd->curr.type == TOKEN_OP && is_double_addsub(fd->curr.p, fd->curr.len)) {
      _read(fd, 0);
    }

    // actually match a value-able thing
    if (fd->curr.type == TOKEN_STRING) {
      _read(fd, 0);

      // match inner parts of template literal string
      while (fd->curr.type == TOKEN_T_BRACE) {
        _ret(state__value(fd, 1));

        if (fd->curr.type != TOKEN_CLOSE) {
          return ERROR__SYNTAX;
        }
        _read(fd, 0);
        if (fd->curr.type != TOKEN_STRING) {
          return ERROR__SYNTAX;
        }
        _read(fd, 0);  // now resting on thing after string again
      }
      continue;

    } else if (fd->curr.type == TOKEN_NUMBER || fd->curr.type == TOKEN_REGEXP) {
      // easy, number or regexp
      _read(fd, 0);
      continue;

    } else if (fd->curr.type == TOKEN_PAREN) {
      return ERROR__TODO;
    } else if (fd->curr.type == TOKEN_ARRAY) {
      return ERROR__TODO;
    } else if (fd->curr.type == TOKEN_BRACE) {
      return ERROR__TODO;  // dict
    } else if (fd->curr.type != TOKEN_LIT) {

      // are we missing anything?

      printf("got unhandled value: %d\n", fd->curr.type);
      return ERROR__SYNTAX;
    }

    // TODO: more
    return 0;
  }
}

static int state__expand_decl(feeddef *fd) {
  _ret(_read(fd, 0));

  switch (fd->curr.type) {
    case TOKEN_BRACE:
    case TOKEN_ARRAY:
      return ERROR__TODO;

    case TOKEN_LIT:
      // can't declare "var if" or "catch (if)"
      if (!is_always_keyword(fd->curr.p, fd->curr.len)) {
        break;
      }
      fd->curr.type = TOKEN_KEYWORD;

      // fall-through
    default:
      return ERROR__SYNTAX;
  }

  fd->curr.type = TOKEN_SYMBOL;
  return 0;
}

static int state__decl(feeddef *fd) {
  // nb. called after "var", focus is on first var name

  for (;;) {
    _ret(state__expand_decl(fd));

    // read assignment of thing
    if (fd->next->type == TOKEN_OP && token_string(fd->next, "=", 1)) {
      _read(fd, 0);  // read op
      _ret(state__value(fd, 1));
    }

    // more variable decls
    if (fd->next->type != TOKEN_COMMA) {
      return 0;
    }
    _read(fd, 0);  // consumes comma
  }
}

static int _read_maybe_semicolon(feeddef *fd) {
  token *n = fd->next;

  if (n->type == TOKEN_SEMICOLON) {
    _read(fd, 0);
  } else if (n->line_no != fd->curr.line_no || n->type == TOKEN_CLOSE || n->type == TOKEN_EOF) {
    _read_asi(fd);
  } else {
    return 0;  // does NOT read
  }
  return 1;
}

static int _read_ensure_semicolon(feeddef *fd) {
  if (_read_maybe_semicolon(fd)) {
    return 0;
  }
  _read(fd, 0);  // read invalid
  return ERROR__SYNTAX;
}

static int state__block(feeddef *fd, int is_next) {
  if (is_next) {
    _ret(_read(fd, 0));
  }
  if (fd->curr.type != TOKEN_BRACE) {
    return ERROR__SYNTAX;
  }

  _ret(state__slist(fd));
  _ret(_read(fd, 0));

  if (fd->curr.type != TOKEN_CLOSE || fd->curr.p[0] != '}') {
    return ERROR__SYNTAX;
  }
  return 0;
}

static int state__paren(feeddef *fd, int flag) {
  if (fd->next->type != TOKEN_PAREN) {
    if (flag == _PAREN_CATCH) {
      return 0;  // this is fine
    }
    _read(fd, 0);
    return ERROR__SYNTAX;
  }

  _read(fd, 0);  // we know this is TOKEN_PAREN

  if (flag == _PAREN_FOR) {
    // 1st part of for loop
    if (fd->next->type != TOKEN_SEMICOLON) {
      _read(fd, 0);

      // match declaration start "for (var x = ..."
      if (is_decl_start(&(fd->curr), fd->next)) {
        fd->curr.type = TOKEN_KEYWORD;
        _ret(state__decl(fd));
      } else {
        // otherwise it's just a value
        _ret(state__value(fd, 0));
      }
    }
    _read(fd, 0);

    // 2nd part of for loop
    if (fd->next->type != TOKEN_SEMICOLON) {
      _ret(state__value(fd, 1));
    }
    _read(fd, 0);

    // 3rd part of for loop
    if (fd->next->type != TOKEN_CLOSE) {
      _ret(state__value(fd, 1));
    }
  } else if (flag == _PAREN_DEF) {
    // "function foo (x=123, y=456)"
    if (fd->next->type != TOKEN_CLOSE) {
      // can close immediately here
      _ret(state__decl(fd));
    }
  } else if (flag == _PAREN_CATCH) {
    // read decl-like thing "catch ([x,y]) {}"
    _ret(state__expand_decl(fd));
  } else {
    // non-for: must consume a single value
    _ret(state__value(fd, 1));
  }

  _ret(_read(fd, 0));
  if (fd->curr.type != TOKEN_CLOSE || fd->curr.p[0] != ')') {
    return ERROR__SYNTAX;
  }

  return 0;
}

static int state__value_function(feeddef *fd) {
  // always starts "on" function
  // FIXME: basic functions only, no async/*

  if (fd->next->type == TOKEN_LIT) {
    _read(fd, 0);

    if (is_always_keyword(fd->curr.p, fd->curr.len)) {
      return ERROR__SYNTAX;
    }
  }

  _ret(state__paren(fd, _PAREN_DEF));
  return state__block(fd, 1);
}

static int state__value_class(feeddef *fd) {
  // always starts "on" class

  if (fd->next->type == TOKEN_LIT) {
    _read(fd, 0);
    int extends = 0;

    if (token_string(&(fd->curr), "extends", 7)) {
      fd->curr.type = TOKEN_KEYWORD;
      extends = 1;
    } else if (is_always_keyword(fd->curr.p, fd->curr.len)) {
      return ERROR__SYNTAX;
    } else {
      fd->curr.type = TOKEN_SYMBOL;

      if (fd->next->type == TOKEN_LIT && token_string(fd->next, "extends", 7)) {
        _read(fd, 0);
        fd->curr.type = TOKEN_KEYWORD;
        extends = 1;
      }
    }

    // optionally consume what we're extending
    if (extends) {
      // FIXME: this should only be a single value, doesn't allow e.g. 1+1
      // FIXME: but allows "new foo" (seemingly nothing else)
      _ret(state__value(fd, 1));
    }
  }

  // TODO: consume class body
  _read(fd, 0);
  return ERROR__TODO;
}

static int state__s(feeddef *fd) {
restart_s:
  _ret(_read(fd, 0));

  // literally nothing
  if (fd->curr.type == TOKEN_SEMICOLON) {
    return 0;
  }

  // match orphaned {...}
  if (fd->curr.type == TOKEN_BRACE) {
    return state__block(fd, 0);
  }

  // match special non-value options
  do {
    if (fd->curr.type != TOKEN_LIT) {
      break;
    }

    // match var/const/let (sometimes)
    if (is_decl_start(&(fd->curr), fd->next)) {
      fd->curr.type = TOKEN_KEYWORD;
      _ret(state__decl(fd));
      return _read_ensure_semicolon(fd);
    }

    // look for "break foo;" or "continue bar;" (no newline allowed)
    if (is_label_keyword(fd->curr.p, fd->curr.len)) {
      fd->curr.type = TOKEN_KEYWORD;

      if (_read_maybe_semicolon(fd)) {
        return 0;  // done, matched semicolon/ASI after keyword
      }

      _read(fd, 0);
      if (fd->curr.type != TOKEN_LIT || is_always_keyword(fd->curr.p, fd->curr.len)) {
        return ERROR__SYNTAX;
      }
      fd->curr.type = TOKEN_LABEL;
      return _read_ensure_semicolon(fd);
    }

    // look for "throw VALUE"
    if (token_string(&(fd->curr), "throw", 5)) {
      fd->curr.type = TOKEN_KEYWORD;

      if (fd->curr.line_no != fd->next->line_no) {
        return ERROR__SYNTAX;  // doesn't insert ASI, just plain fails ("throw;" is invalid)
      }
      _read(fd, 0);
      break;  // fall-through to value matcher
    }

    // look for "return VALUE"
    if (token_string(&(fd->curr), "return", 6)) {
      fd->curr.type = TOKEN_KEYWORD;

      if (_read_maybe_semicolon(fd)) {
        return 0;  // done, matched semicolon/ASI after keyword
      }
      _read(fd, 0);
      break;  // fall-through to value matcher below
    }

    // look for "label:"
    if (fd->next->type == TOKEN_COLON) {
      if (is_reserved_word(fd->curr.p, fd->curr.len)) {
        _read(fd, 0);
        return ERROR__SYNTAX;
      }

      fd->curr.type = TOKEN_LABEL;
      _read(fd, 0);  // consume colon
      goto restart_s;
    }

    // "if (x)" and similar
    if (is_control_paren(fd->curr.p, fd->curr.len)) {
      fd->curr.type = TOKEN_KEYWORD;

      char start_char = fd->curr.p[0];
      if (start_char == 'c') {
        return ERROR__SYNTAX;  // "catch" not allowed here
      }

      int flag = 0;
      if (start_char == 'f') {
        flag = _PAREN_FOR;
      }

      // match paren block "if (x == 1)"
      _ret(state__paren(fd, flag));

      if (start_char == 's') {
        return ERROR__TODO;  // match switch/cases
      }

      _ret(state__s(fd));

      // allow "else"
      if (start_char == 'i' && fd->next->type == TOKEN_LIT && token_string(fd->next, "else", 4)) {
        _read(fd, 0);
        fd->curr.type = TOKEN_KEYWORD;
        goto restart_s;
      }

      return 0;
    }

    // "try"
    if (token_string(&(fd->curr), "try", 3)) {
      fd->curr.type = TOKEN_KEYWORD;
      _ret(state__block(fd, 1));
      int seen_either = 0;

      // allow trailing "catch"
      if (fd->next->type == TOKEN_LIT && token_string(fd->next, "catch", 5)) {
        seen_either = 1;
        _read(fd, 0);
        fd->curr.type = TOKEN_KEYWORD;

        _ret(state__paren(fd, _PAREN_CATCH));
        _ret(state__block(fd, 1));
      }

      // allow trailing "finally"
      if (fd->next->type == TOKEN_LIT && token_string(fd->next, "finally", 5)) {
        seen_either = 1;
        _read(fd, 0);
        fd->curr.type = TOKEN_KEYWORD;

        _ret(state__block(fd, 1));
      }

      if (!seen_either) {
        // nb. error here is on closing } of try
        return ERROR__SYNTAX;
      }
      return 0;
    }

    // "do"
    if (token_string(&(fd->curr), "do", 2)) {
      fd->curr.type = TOKEN_KEYWORD;

      _ret(state__s(fd));

      _read(fd, 0);  // always read next
      if (fd->curr.type != TOKEN_LIT || !token_string(&(fd->curr), "while", 5)) {
        return ERROR__SYNTAX;
      }
      fd->curr.type = TOKEN_KEYWORD;
      return state__paren(fd, 0);  // conditional after "while"
      // nb. "do ; while(0)" does not require a trailing semicolon or ASI
    }

    // hoisted class
    if (token_string(&(fd->curr), "class", 5)) {
      fd->curr.type = TOKEN_KEYWORD;

      if (fd->next->type != TOKEN_LIT || token_string(fd->next, "extends", 7)) {
        _read(fd, 0);
        return ERROR__SYNTAX;  // hoisted class must have name
      }
      return state__value_class(fd);
    }

    // "async" (as optional precursor to function)
    int has_async = 0;
    if (token_string(&(fd->curr), "async", 5)) {
      has_async = 1;
    }

    if (token_string(&(fd->curr), "function", 8)) {
      return state__value_function(fd);
    }

  } while (0);

  // match statement-like value
  _ret(state__value(fd, 0));
  return _read_ensure_semicolon(fd);
}

static int state__module(feeddef *fd) {
  // consume import or export statements
  _read(fd, 0);
  return ERROR__TODO;
}

/**
 * consume a list of statements, stop before EOF or TOKEN_CLOSE depending on context
 */
static int state__slist(feeddef *fd) {
  int is_top = (fd->curr.type != TOKEN_BRACE);
  token *n = fd->next;

  for (;;) {
    if (is_top) {
      if (n->type == TOKEN_EOF) {
        return 0;
      }

      // peek for module code, only allowed at top
      if (n->type == TOKEN_LIT) {
        if (token_string(n, "import", 6) || token_string(n, "export", 6)) {
          _ret(state__module(fd));
          continue;
        }
      }

    } else if (n->type == TOKEN_CLOSE) {
      return 0;
    }

    // read statement
    _ret(state__s(fd));
  }
}

int prsr_feed(tokendef *td, prsr_callback cb, void *arg) {
  feeddef fd;
  bzero(&fd, sizeof(feeddef));

  fd.cb = cb;
  fd.arg = arg;
  fd.td = td;
  fd.next = &(td->next);  // for convenience

  int out = state__slist(&fd);
  if (!out) {
    _read(&fd, 0);  // read final EOF
  }
  cb(arg, &(fd.curr));  // emit trailing thing, could be the invalid reason
  return out;
}
