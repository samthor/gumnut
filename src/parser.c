/*
 * Copyright 2020 Sam Thorogood.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */

#include <ctype.h>
#include "parser.h"
#include "helper.h"
#include "token.h"

#define _check(v) { int _ret = v; if (_ret) { return _ret; }};

#ifdef DEBUG
#include <stdio.h>
#define debugf(...) fprintf(stderr, __VA_ARGS__)
#else
#define debugf (void)sizeof
#endif


static int top_context;

static int consume_statement(int);
static int consume_expr_group(int);
static int consume_optional_expr(int);
static int consume_class(int);
static int consume_module_list(int);
static int consume_function(int);
static int consume_expr_compound(int);

static inline void internal_next_comment() {
  for (;;) {
    int out = prsr_next();
    if (out != TOKEN_COMMENT) {
      break;
    }
    modp_callback(0);
  }
}

// yields previous, places the next useful token in curr, skipping comments
static void internal_next() {
  modp_callback(0);
  internal_next_comment();
}

static void internal_next_update(int type) {
  // TODO: we don't care about return type here
  prsr_update(type);
  modp_callback(0);
  internal_next_comment();
}

static int consume_import_module_special() {
  if (td.cursor.type != TOKEN_STRING) {
    debugf("no string found in import/export\n");
    return ERROR__UNEXPECTED;
  }

  int len = td.cursor.len;
  if (len == 1 || (td.cursor.p[0] == '`' && td.cursor.p[len - 1] != '`')) {
    // nb. probably an error, but will just be treated as an expr
    return 0;
  }

  modp_callback(SPECIAL__MODULE_PATH);
  internal_next_comment();
  return 0;
}

int consume_import(int context) {
  if (td.cursor.hash != LIT_IMPORT) {
    debugf("missing import keyword\n");
    return ERROR__UNEXPECTED;
  }
  internal_next_update(TOKEN_KEYWORD);

  if (td.cursor.type != TOKEN_STRING) {
    _check(consume_module_list(context));

    // consume "from"
    if (td.cursor.hash != LIT_FROM) {
      debugf("missing from keyword\n");
      return ERROR__UNEXPECTED;
    }
    internal_next_update(TOKEN_KEYWORD);
  }

  // match string (but not if `${}`)
  return consume_import_module_special();
}

int consume_export(int context) {
  if (td.cursor.hash != LIT_EXPORT) {
    debugf("missing export keyword\n");
    return ERROR__UNEXPECTED;
  }
  internal_next_update(TOKEN_KEYWORD);

  int is_default = 0;
  if (td.cursor.hash == LIT_DEFAULT) {
    internal_next_update(TOKEN_SYMBOL);
    is_default = 1;
  }

  // if this is class/function, consume with no value
  switch (td.cursor.hash) {
    case LIT_CLASS:
      return consume_class(context);

    case LIT_ASYNC:
      prsr_peek();
      if (td.peek.hash != LIT_FUNCTION) {
        break;
      }
      // fall-through

    case LIT_FUNCTION:
      return consume_function(context);
  }

  // "export {..." or "export *" or "export default *"
  if ((!is_default && td.cursor.type == TOKEN_BRACE) || td.cursor.hash == MISC_STAR) {
    _check(consume_module_list(context));

    // consume optional "from"
    if (td.cursor.hash != LIT_FROM) {
      return 0;
    }
    internal_next_update(TOKEN_KEYWORD);

    // match string (but not if `${}`)
    return consume_import_module_special();
  }

  int has_decl = (td.cursor.hash & _MASK_DECL) ? 1 : 0;
  if (has_decl == is_default) {
    // can't default export "var foo", can't _not_ "export var".
    // TODO: should fail, allow for now
  }

  // otherwise, treat as expr (this catches var/let/const too)
  return consume_expr_compound(context);
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
    debugf("missing 'function' keyword\n");
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
    debugf("missing 'async' starter\n");
    return ERROR__UNEXPECTED;
  }

  int peek_type = prsr_peek();
  switch (peek_type) {
    case TOKEN_LIT:
      if (td.peek.hash == LIT_FUNCTION) {
        return consume_function(context);
      }

      // "async namehere"
      internal_next_update(TOKEN_KEYWORD);
      internal_next_update(TOKEN_SYMBOL);

      if (td.cursor.hash != MISC_ARROW) {
        debugf("could not match arrow after 'async foo'\n");
        return ERROR__UNEXPECTED;
      }
      break;

    case TOKEN_PAREN:
      internal_next();  // nb. we explicitly yield TOKEN_LIT here

      // _maybe_ function
      _check(consume_expr_group(context));

      if (td.cursor.hash != MISC_ARROW) {
        return 0;  // not a function, just bail (has value)
      }
      break;

    case TOKEN_OP:
      if (td.peek.hash == MISC_ARROW) {
        internal_next_update(TOKEN_KEYWORD);
        // nb. allow "async =>" even though broken
        break;
      }
      // fall-through

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

int consume_module_list(int context) {
  for (;;) {
    if (td.cursor.type == TOKEN_BRACE) {
      internal_next();
      _check(consume_module_list(context));
      if (td.cursor.type != TOKEN_CLOSE) {
        debugf("missing close after module list\n");
        return ERROR__UNEXPECTED;
      }
      internal_next();
    } else {
      // can start with "*", "foo", and end with "as blah"
      if (td.cursor.type == TOKEN_OP) {
        if (td.cursor.hash != MISC_STAR) {
          return 0;
        }
        internal_next();
      } else if (td.cursor.type == TOKEN_LIT) {
        internal_next_update(TOKEN_SYMBOL);
      } else {
        return 0;
      }

      // catch optional "as x"
      if (td.cursor.hash == LIT_AS) {
        internal_next_update(TOKEN_KEYWORD);
        if (td.cursor.type != TOKEN_LIT) {
          debugf("missing literal after 'as'\n");
          return ERROR__UNEXPECTED;
        }
        internal_next_update(TOKEN_SYMBOL);
      }
    }

    if (td.cursor.hash != MISC_COMMA) {
      return 0;
    }
    internal_next();
  }
}

// consumes dict or class (allows either)
static int consume_dict(int context) {
  if (td.cursor.type != TOKEN_BRACE) {
    debugf("missing open brace for dict\n");
    return ERROR__UNEXPECTED;
  }
  internal_next();

  for (;;) {
    int method_context = context;

    if (td.cursor.hash == MISC_SPREAD) {
      internal_next();
      _check(consume_optional_expr(context));
      continue;
    }

    // static prefix
    if (td.cursor.hash == LIT_STATIC && prsr_peek() != TOKEN_PAREN) {
      internal_next_update(TOKEN_KEYWORD);
    }

    // "async" prefix
    if (td.cursor.hash == LIT_ASYNC && prsr_peek() != TOKEN_PAREN) {
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
    if ((td.cursor.hash == LIT_GET || td.cursor.hash == LIT_SET) && prsr_peek() != TOKEN_PAREN) {
      internal_next_update(TOKEN_KEYWORD);
    }

    // name or bracketed name
    switch (td.cursor.type) {
      case TOKEN_LIT:
        internal_next_update(TOKEN_SYMBOL);
        break;

      case TOKEN_NUMBER:
        internal_next();
        break;

      case TOKEN_ARRAY:
      case TOKEN_STRING:
        _check(consume_expr_group(context));
        break;

      default:
        ;
        // ignore missing name, whatever
    }

    // check terminal case (which decides what tpye of entry this is), method or equal/colon
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

      case TOKEN_OP:
        if (td.cursor.hash != MISC_EQUALS) {
          break;
        }
        // fall-through

      case TOKEN_COLON:
        // nb. this allows "async * foo:" or "async foo =" which is nonsensical
        internal_next();
        _check(consume_optional_expr(context));
        break;
    }

    // handle tail cases (close, eof, op, etc)
    switch (td.cursor.type) {
      case TOKEN_CLOSE:
        internal_next();
        return 0;

      case TOKEN_EOF:
        // don't stay here forever
        debugf("got EOF inside dict\n");
        return ERROR__UNEXPECTED;

      case TOKEN_OP:
        if (td.cursor.hash != MISC_COMMA) {
          break;
        }
        // fall-through

      case TOKEN_SEMICOLON:
        internal_next();
        continue;

      case TOKEN_LIT:
      case TOKEN_NUMBER:
      case TOKEN_STRING:
      case TOKEN_ARRAY:
        continue;
    }

    debugf("unknown left-side dict part: %d\n", td.cursor.type);
    return ERROR__UNEXPECTED;
  }
}

static int consume_optional_expr(int context) {
  int value_line = 0;  // line_no of last value
  int seen_any = 0;
#define _transition_to_value() { if (value_line) { return 0; } value_line = td.cursor.line_no; seen_any = 1; }

  for (;;) {
    switch (td.cursor.type) {
      case TOKEN_SLASH:
        _check(prsr_update(value_line ? TOKEN_OP : TOKEN_REGEXP));
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
            // "x of y": only make sense in value-like contexts and is nonsensical anywhere else
            if (value_line) {
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

        _check(prsr_update(type));
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

          case MISC_ARROW: {
            internal_next();
            int normal_context = context & ~(CONTEXT__ASYNC);
            if (td.cursor.type == TOKEN_BRACE) {
              _check(consume_statement(normal_context));
            } else {
              _check(consume_optional_expr(normal_context));
            }
            seen_any = 1;
            // nb. this has value, but always ends with ) or , etc
            continue;
          }

          case MISC_NOT:
          case MISC_BITNOT:
            // nb. this matches _MASK_UNARY_OP above
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
static int consume_expr_compound(int context) {
  for (;;) {
    _check(consume_optional_expr(context));
    if (td.cursor.hash != MISC_COMMA) {
      break;
    }
    internal_next();
  }
  return 0;
}

static int consume_expr_group(int context) {
  int start = td.cursor.type;
  switch (td.cursor.type) {
    case TOKEN_STRING:
      // special-case strings
      internal_next();
      for (;;) {
        if (td.cursor.type != TOKEN_T_BRACE) {
          return 0;
        }
        _check(consume_expr_group(context));
        internal_next();  // this must be string
      }
      return ERROR__INTERNAL;  // should not get here

    case TOKEN_PAREN:
    case TOKEN_ARRAY:
    case TOKEN_TERNARY:
    case TOKEN_T_BRACE:
      break;

    default:
      debugf("expected expr group\n");
      return ERROR__UNEXPECTED;
  }
  internal_next();

  for (;;) {
    _check(consume_expr_compound(context));

    // nb. not really good practice, but handles for-loop-likes
    if (td.cursor.type != TOKEN_SEMICOLON) {
      break;
    }

    internal_next();
  }

  if (td.cursor.type != TOKEN_CLOSE) {
    debugf("expected close after expr group\n");
    return ERROR__UNEXPECTED;
  }
  internal_next();
  return 0;
}

static int consume_class(int context) {
  if (td.cursor.hash != LIT_CLASS) {
    debugf("expected class keyword\n");
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

static int consume_statement(int context) {
  switch (td.cursor.type) {
    case TOKEN_EOF:
      return 0;

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
            debugf("no : after default\n");
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
            debugf("no : after case\n");
            return ERROR__UNEXPECTED;
          }
          internal_next();
          return 0;

        case LIT_ASYNC: {
          int peek_type = prsr_peek();
          if (peek_type == TOKEN_LIT && td.peek.hash == LIT_FUNCTION) {
            // only match "async function", as others are expr (e.g. "async () => {}")
            return consume_function(context);
          }
          // fall-through
        }
      }

      if (!(td.cursor.hash & _MASK_MASQUERADE) && prsr_peek() == TOKEN_COLON) {
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
    // ... it's not important that while(); is parsed as part of do-while.
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
      internal_next_update(TOKEN_KEYWORD);

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
        // TODO: could we detect "return" + expr and warn?
        return 0;
      }
      return consume_expr_compound(context);
      // nb. should look for semi here, just ignore
    }

    case LIT_IMPORT: {
      prsr_peek();
      if (td.peek.type == TOKEN_PAREN || td.peek.hash == MISC_DOT) {
        break;  // treat as expr
      }
      return consume_import(context);
    }

    case LIT_EXPORT:
      return consume_export(context);

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

  return consume_expr_compound(context);
}

token *modp_init(char *p, int _context) {
  prsr_init_token(p);
  top_context = _context;

  if (p[0] == '#' && p[1] == '!') {
    // special-case hashbang opener
    td.cursor.type = TOKEN_COMMENT;
    td.cursor.len = strline(p);
    td.cursor.line_no = 1;
  } else {
    // n.b. it's possible but unlikely for this to fail (e.g. opens with "}")
    prsr_next();
  }

  return &(td.cursor);
}

int modp_run() {
  char *head = td.cursor.p;

  while (td.cursor.type == TOKEN_COMMENT) {
    modp_callback(0);
    prsr_next();
  }
  _check(consume_statement(top_context));

  int len = td.cursor.p - head;
  if (len == 0 && td.cursor.type != TOKEN_EOF) {
    debugf("expr did not get consumed\n");
    return ERROR__UNEXPECTED;
  }
  return len;
}
