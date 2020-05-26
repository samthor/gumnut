#include "parser.h"
#include "tokens/lit.h"
#include "token.h"

#define _check(v) { int _ret = v; if (_ret) { return _ret; }};

#ifdef DEBUG
#include <stdio.h>
#define debugf(...) fprintf(stderr, __VA_ARGS__)
#else
#define debugf (void)sizeof
#endif


static tokendef td; 
static prsr_callback callback;
static void *arg;

int consume_statement(int);
int consume_expr_group(int);
int consume_optional_expr(int);
int consume_class(int);

static inline void internal_next_find() {
  for (;;) {
    int out = prsr_next(&td);
    if (out != TOKEN_COMMENT) {
      break;
    }
    callback(arg, &(td.cursor));
  }
}

// yields previous, places the next useful token in curr, skipping comments
static void internal_next() {
  callback(arg, &(td.cursor));
  internal_next_find();
}

static void internal_next_update(int type) {
  prsr_update(&td, type);
  internal_next();
}

// consumes "async function foo ()"
int consume_function(int context) {
  int statement_context = context;

  // check for leading async and update context
  if (td.cursor.hash == LIT_ASYNC) {
    statement_context |= CONTEXT__ASYNC;
    internal_next_update(TOKEN_KEYWORD);
  } else {
    statement_context &= ~(CONTEXT__ASYNC);
  }

  // expect function literal
  if (td.cursor.hash != LIT_FUNCTION) {
    debugf("could not consume 'function'\n");
    return ERROR__UNEXPECTED;
  }
  internal_next_update(TOKEN_KEYWORD);

  // check for generator star
  if (td.cursor.hash == MISC_STAR) {
    internal_next();
  }

  // check for optional function name
  if (td.cursor.type == TOKEN_LIT) {
    internal_next_update(TOKEN_SYMBOL);  // nb. should ban reserved words
  }

  // check for parens (nb. should be required)
  if (td.cursor.type == TOKEN_PAREN) {
    _check(consume_expr_group(context));
  }

  // consume function body
  return consume_statement(statement_context);
}

// consumes something starting with async (might be function)
int consume_async_expr(int context) {
  if (td.cursor.hash != LIT_ASYNC) {
    debugf("could not match 'async'\n");
    return ERROR__UNEXPECTED;
  }

  int peek_type = prsr_peek(&td);
  switch (peek_type) {
    case TOKEN_LIT:
      // function, this is a variable name
      internal_next_update(TOKEN_KEYWORD);
      internal_next_update(TOKEN_SYMBOL);

      if (td.cursor.type != TOKEN_ARROW) {
        debugf("could not match arrow after 'async foo'\n");
        return ERROR__UNEXPECTED;
      }
      break;

    case TOKEN_PAREN:
      internal_next();  // nb. we explicitly yield TOKEN_LIT here

      // _maybe_ function
      _check(consume_expr_group(context));

      if (td.cursor.type != TOKEN_ARROW) {
        return 0;  // not a function, just bail (has value)
      }
      break;

    default:
      internal_next_update(TOKEN_SYMBOL);
      return 0;  // unhandled
  }

  internal_next();  // consume arrow

  int async_context = context | CONTEXT__ASYNC;
  if (td.cursor.type == TOKEN_BRACE) {
    _check(consume_statement(async_context));
  } else {
    _check(consume_optional_expr(async_context));
  }

  return 0;
}

// consumes dict or class (allows either)
int consume_dict(int context) {
  if (td.cursor.type != TOKEN_BRACE) {
    debugf("could not find starting { of dict\n");
    return ERROR__UNEXPECTED;
  }
  debugf(">> dict\n");
  internal_next();

  for (;;) {
    int method_context = context;

    if (td.cursor.hash == MISC_SPREAD) {
      internal_next();
      _check(consume_optional_expr(context));
      continue;
    }

    // static prefix
    if (td.cursor.hash == LIT_STATIC && prsr_peek(&td) != TOKEN_PAREN) {
      internal_next_update(TOKEN_KEYWORD);
    }

    // "async" prefix
    if (td.cursor.hash == LIT_ASYNC && prsr_peek(&td) != TOKEN_PAREN) {
      // "async(" is a valid function name, sigh
      method_context |= CONTEXT__ASYNC;
      internal_next_update(TOKEN_KEYWORD);
    } else {
      method_context &= ~(CONTEXT__ASYNC);
    }

    // generator
    if (td.cursor.hash == MISC_STAR) {
      internal_next();
    }

    // get/set without bracket
    if ((td.cursor.hash == LIT_GET || td.cursor.hash == LIT_SET) && prsr_peek(&td) != TOKEN_PAREN) {
      internal_next_update(TOKEN_KEYWORD);
    }

    // name or bracketed name
    if (td.cursor.type == TOKEN_LIT) {
      internal_next_update(TOKEN_SYMBOL);
    } else if (td.cursor.type == TOKEN_ARRAY) {
      _check(consume_expr_group(context));
    } else {
      // ignore missing name, whatever
    }

    switch (td.cursor.type) {
      case TOKEN_PAREN:
        // method
        _check(consume_expr_group(context));

        if (td.cursor.type != TOKEN_BRACE) {
          debugf("did not find brace after dict paren\n");
          return ERROR__UNEXPECTED;
        }
        _check(consume_statement(method_context));
        break;

      case TOKEN_COLON:
        // value
        // nb. this allows "async * foo:" which is nonsensical
        internal_next();
        _check(consume_optional_expr(context));
        break;

      case TOKEN_CLOSE:
        // close, handled below
        break;

      case TOKEN_OP:
        if (td.cursor.hash == MISC_COMMA) {
          // single value (e.g. "{foo,bar}")
          internal_next();
          continue;
        }
        // fall-through

      default:
        // nb. this should fail, but who cares
        _check(consume_optional_expr(context));
        continue;
    }

    // keep going, more data
    if (td.cursor.hash == MISC_COMMA) {
      internal_next();
      continue;
    }

    // closed
    if (td.cursor.type == TOKEN_CLOSE) {
      internal_next();
      return 0;
    }

    debugf("got to end of dict\n");
    return ERROR__UNEXPECTED;
  }
}

int consume_optional_expr(int context) {
  int value_line = 0;  // line_no of last value
  int seen_any = 0;
#define _transition_to_value() { if (value_line) { return 0; } value_line = td.cursor.line_no; seen_any = 1; }

  for (;;) {
    switch (td.cursor.type) {
      case TOKEN_SLASH:
        prsr_update(&td, value_line ? TOKEN_OP : TOKEN_REGEXP);
        continue;  // restart without move

      case TOKEN_BRACE:
        if (seen_any && value_line) {
          // e.g. "(foo {})" is invalid, but "(foo + {})" is ok
          return 0;
        }
        _check(consume_dict(context));
        value_line = td.cursor.line_no;
        seen_any = 1;
        continue;

      case TOKEN_TERNARY:
        // nb. needs value on left (and contents!), but nonsensical otherwise
        _check(consume_expr_group(context));
        value_line = 0;
        seen_any = 1;
        continue;

      case TOKEN_T_BRACE:
        _check(consume_expr_group(context));
        value_line = 0;  // allows string to continue
        seen_any = 1;
        continue;

      case TOKEN_ARRAY:
      case TOKEN_PAREN:
        _check(consume_expr_group(context));
        value_line = td.cursor.line_no;
        seen_any = 1;
        continue;

      case TOKEN_STRING:
        if (td.cursor.p[0] == '`' && value_line) {
          // tagged template, e.g. "hello`123`"
          internal_next();
          continue;
        }
        // fall-through

      case TOKEN_SYMBOL:  // for calling again via async
      case TOKEN_REGEXP:
      case TOKEN_NUMBER:
        _transition_to_value();
        internal_next();
        continue;

      case TOKEN_ARROW: {
        internal_next();
        int normal_context = context & ~(CONTEXT__ASYNC);
        if (td.cursor.type == TOKEN_BRACE) {
          _check(consume_statement(normal_context));
        } else {
          _check(consume_optional_expr(normal_context));
        }
        // nb. this has value, but always ends with ) or , etc
        continue;
      }

      case TOKEN_LIT: {
        int type = TOKEN_SYMBOL;

        switch (td.cursor.hash) {
          case LIT_VAR:
          case LIT_LET:
          case LIT_CONST:
            if (seen_any) {
              return 0;
            }
            // nb. only keyword at top-level and in for, but nonsensical otherwise
            internal_next_update(TOKEN_KEYWORD);
            continue;

          case LIT_ASYNC:
            _transition_to_value();
            _check(consume_async_expr(context));
            continue;

          case LIT_FUNCTION:
            _transition_to_value();
            _check(consume_function(context));
            continue;

          case LIT_CLASS:
            _transition_to_value();
            _check(consume_class(context));
            continue;

        // nb. below here are mostly migrations (symbol or op)

          case LIT_OF:
            if (value_line) {
              // nb. this is invalid outside a "for (x of y)", but nonsensical anywhere else
              // TODO: except for line breaks?
              type = TOKEN_OP;
            }
            break;

          case LIT_AWAIT:
            if (context & CONTEXT__ASYNC) {
              type = TOKEN_OP;
            }
            break;

          default:
            if (td.cursor.hash & (_MASK_REL_OP | _MASK_UNARY_OP)) {
              type = TOKEN_OP;
            } else if (td.cursor.hash & _MASK_KEYWORD) {
              return 0;
            } else if (value_line) {
              return 0;  // value after value
            }
        }

        prsr_update(&td, type);
        continue;
      }

      case TOKEN_OP:
        if (td.cursor.hash & _MASK_UNARY_OP) {
          if (seen_any && value_line) {
            // e.g., "var x = 123 new foo" is invalid
            return 0;
          }
        }

        switch (td.cursor.hash) {
          case MISC_COMMA:
            return 0;

          case MISC_NOT:
          case MISC_BITNOT:
            // TODO: "await, new, etc" fall into this bucket?
            if (seen_any && value_line) {
              // explicitly only takes right arg, so e.g.:
              //   "var x = 123 !foo"
              // ...is invalid.
              return 0;
            }
            break;

          case MISC_DOT:
          case MISC_CHAIN:
            // nb. this needs has_value, but it's nonsensical otherwise
            internal_next();
            if (td.cursor.type != TOKEN_LIT) {
              return 0;
            }
            internal_next_update(TOKEN_SYMBOL);
            value_line = td.cursor.line_no;
            seen_any = 1;
            continue;

          case MISC_INCDEC:
            if (!value_line) {
              // ok, attaches to upcoming
            } else if (td.cursor.line_no != value_line) {
              // on new line, not attached to previous
              return 0;
            }
            break;

          default:
            value_line = 0;
        }

        internal_next();
        continue;
    }

    return 0;
  }
#undef _transition_to_value
}

// consumes a compound expr separated by ,'s
int consume_expr_compound(int context) {
  for (;;) {
    _check(consume_optional_expr(context));
    if (td.cursor.hash != MISC_COMMA) {
      break;
    }
    internal_next();
  }
  return 0;
}

int consume_expr_group(int context) {
  int start = td.cursor.type;
  switch (td.cursor.type) {
    case TOKEN_PAREN:
    case TOKEN_ARRAY:
    case TOKEN_TERNARY:
    case TOKEN_T_BRACE:
      break;

    default:
      return ERROR__UNEXPECTED;
  }
  internal_next();

  for (;;) {
    _check(consume_expr_compound(context));

    // nb. not really good practice, but handles for-loop-likes
    // TODO(samthor): allow more things
    if (td.cursor.type != TOKEN_SEMICOLON) {
      break;
    }

    internal_next();
  }

  if (td.cursor.type != TOKEN_CLOSE) {
    return ERROR__UNEXPECTED;
  }
  internal_next();
  return 0;
}

int consume_class(int context) {
  if (td.cursor.hash != LIT_CLASS) {
    return ERROR__UNEXPECTED;
  }
  internal_next_update(TOKEN_KEYWORD);

  if (td.cursor.type == TOKEN_LIT) {
    if (td.cursor.hash != LIT_EXTENDS) {
      internal_next_update(TOKEN_SYMBOL);  // nb. should ban reserved words
    }
    if (td.cursor.hash == LIT_EXTENDS) {
      internal_next_update(TOKEN_KEYWORD);

      // nb. something must be here (but if it's not, that's an error, as we expect a '{' following)
      _check(consume_optional_expr(context));
    }
  }

  return consume_dict(context);
}

int consume_statement(int context) {
  switch (td.cursor.type) {
    case TOKEN_BRACE:
      internal_next();  // consume brace, get next

      while (td.cursor.type != TOKEN_CLOSE) {
        int ret = consume_statement(context);
        if (ret != 0) {
          return ret;
        } else if (td.cursor.type == TOKEN_EOF) {
          return ERROR__STACK;  // safety otherwise we won't leave for EOF
        }
      }

      internal_next();  // consume close
      return 0;

    case TOKEN_SEMICOLON:
      internal_next();  // consume
      return 0;

    case TOKEN_LIT:
      switch (td.cursor.hash) {
        case LIT_DEFAULT:
          internal_next_update(TOKEN_KEYWORD);
          if (td.cursor.type != TOKEN_COLON) {
            return ERROR__UNEXPECTED;
          }
          internal_next();
          return 0;

        case LIT_CASE:
          internal_next_update(TOKEN_KEYWORD);
          // nb. not really optional, but not important
          _check(consume_optional_expr(context));

          // after expr, expect colon
          if (td.cursor.type != TOKEN_COLON) {
            return ERROR__UNEXPECTED;
          }
          internal_next();
          return 0;

        case LIT_ASYNC: {
          int peek_type = prsr_peek(&td);
          if (peek_type == TOKEN_LIT && td.peek.hash == LIT_FUNCTION) {
            // only match "async function", as others are expr (e.g. "async () => {}")
            return consume_function(context);
          }
          // fall-through
        }
      }

      if (!(td.cursor.hash & _MASK_MASQUERADE) && prsr_peek(&td) == TOKEN_COLON) {
        // nb. "await:" is invalid in async functions, but it's nonsensical anyway
        internal_next_update(TOKEN_LABEL);
        internal_next();  // consume TOKEN_COLON
        return 0;
      }

      break;
  }

  // match "if", "catch" etc
  if (td.cursor.hash & _MASK_CONTROL) {
    int control_hash = td.cursor.hash;
    td.cursor.type = TOKEN_KEYWORD;
    internal_next();

    // match "for" and "for await"
    if (control_hash == LIT_FOR) {
      if (td.cursor.hash == LIT_AWAIT) {
        td.cursor.type = TOKEN_KEYWORD;
        internal_next();
      }
    }

    if (td.cursor.type == TOKEN_PAREN) {
      _check(consume_expr_group(context));
    }
    // TODO: we could remove the following since we're not generating an AST.
    _check(consume_statement(context));

    // special-case trailing "while(...)" for a 'do-while'
    if (control_hash == LIT_DO) {
      // TODO: in case the statement hasn't closed properly
      if (td.cursor.type == TOKEN_SEMICOLON) {
        internal_next();
      }

      if (td.cursor.hash != LIT_WHILE) {
        debugf("could not find while of do-while\n");
        return ERROR__UNEXPECTED;
      }
      internal_next();

      if (td.cursor.type != TOKEN_PAREN) {
        debugf("could not find do-while parens\n");
        return ERROR__UNEXPECTED;
      }
      _check(consume_expr_group(context));
      // nb. ; is not required to trail "while (...)
    }

    return 0;
  }

  switch (td.cursor.hash) {
    case LIT_RETURN:
    case LIT_THROW: {
      int line_no = td.cursor.line_no;
      internal_next_update(TOKEN_KEYWORD);

      // "throw\n" is actually invalid, but we can't do anything about it
      if (line_no != td.cursor.line_no) {
        return 0;
      }
      return consume_expr_compound(context);
      // nb. should look for semi here, just ignore
    }

    case LIT_IMPORT:
    case LIT_EXPORT:
      return ERROR__TODO;  // import/export

    case LIT_CLASS:
      return consume_class(context);

    case LIT_FUNCTION:
      return consume_function(context);

    case LIT_DEBUGGER: {
      int line_no = td.cursor.line_no;
      internal_next_update(TOKEN_KEYWORD);

      if (line_no != td.cursor.line_no) {
        return 0;
      }

      // nb. should look for semi here, ignore
      return 0;
    }

    case LIT_CONTINUE:
    case LIT_BREAK: {
      int line_no = td.cursor.line_no;
      internal_next_update(TOKEN_KEYWORD);
      if (line_no != td.cursor.line_no) {
        return 0;
      }

      // "break foo"
      if (td.cursor.type == TOKEN_LIT) {
        internal_next_update(TOKEN_LABEL);
        if (line_no != td.cursor.line_no) {
          return 0;
        }
      }

      // nb. should look for semi here, ignore
      return 0;
    }
  }

  char *p = td.cursor.p;
  int ret = consume_expr_compound(context);
  if (p == td.cursor.p) {
    debugf("expr did not get consumed\n");
    return ERROR__UNEXPECTED;
  }
  return ret;
}

int prsr_run(char *p, int context, prsr_callback _callback, void *_arg) {
  td = prsr_init_token(p);
  callback = _callback;
  arg = _arg;

  internal_next_find();  // yield initial comments
  while (td.cursor.type != TOKEN_EOF) {
    _check(consume_statement(context));
  }

  return 0;
}
