
#include <string.h>
#include "feed.h"
#include "../utils.h"

#define _PAREN_FOR   1
#define _PAREN_CATCH 2

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
  if (is_next) {
    _ret(_read(fd, 0));
  }

  // FIXME: values are just numbers, for now
  if (fd->curr.type == TOKEN_NUMBER) {
    return 0;
  }

  printf("got unhandled value: %d\n", fd->curr.type);
  return ERROR__TODO;
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

static int _read_ensure_semicolon(feeddef *fd) {
  if (fd->next->type == TOKEN_SEMICOLON) {
    return _read(fd, 0);
  } else if (fd->next->line_no != fd->curr.line_no) {
    return _read_asi(fd);
  } else {
    _read(fd, 0);  // read invalid
    return ERROR__SYNTAX;
  }
}

static int _read_maybe_semicolon(feeddef *fd) {
  if (fd->next->type == TOKEN_SEMICOLON) {
    _read(fd, 0);
  } else if (fd->next->line_no != fd->curr.line_no) {
    _read_asi(fd);
  } else {
    return 0;  // does NOT read
  }
  return 1;
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
      // TODO: also match "var ..."
      _ret(state__value(fd, 1));
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

static int state__s(feeddef *fd) {
restart_s:
  _ret(_read(fd, 0));

  // literally nothing
  if (fd->curr.type == TOKEN_SEMICOLON) {
    return 0;
  }

  // match orphaned {...}
  if (fd->curr.type == TOKEN_BRACE) {
    _ret(state__slist(fd));

    if (fd->next->type != TOKEN_CLOSE || fd->next->p[0] != '}') {
      return ERROR__SYNTAX;
    }
    return _read(fd, 0);
  }

  // match special non-value options
  do {
    if (fd->curr.type != TOKEN_LIT) {
      break;
    }

    // FIXME: `let` is not always keyword, fix later
    if (is_decl_keyword(fd->curr.p, fd->curr.len)) {
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

      _read(fd, 0);  // consume colon
      goto restart_s;
    }

    // "if (x)" and similar
    if (is_control_paren(fd->curr.p, fd->curr.len)) {
      if (fd->curr.p[0] == 'c') {
        return ERROR__SYNTAX;  // "catch" not allowed here
      }

      int flag = 0;
      if (fd->curr.p[0] == 'f') {
        flag = _PAREN_FOR;
      }

      _ret(state__paren(fd, flag));
      return state__s(fd);
    }

    if (token_string(&(fd->curr), "function", 8)) {

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
