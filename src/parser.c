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


#define _modp_callback(v) { \
  if (!skip_context) { \
    modp_callback(v); \
  } \
}

static int skip_context = 0;
static int top_context;

static int consume_statement(int, int);
static int consume_expr_group(int);
static int consume_optional_expr(int);
static int consume_class(int, int);
static int consume_module_list(int);
static int consume_function(int, int);
static int consume_expr_compound(int);
static int consume_definition_list(int);
static int consume_string(int);
static int consume_destructuring(int, int);
static int consume_optional_arg_group(int);

static inline void internal_next_comment() {
  for (;;) {
    int out = prsr_next();
    if (out != TOKEN_COMMENT) {
      break;
    }
    _modp_callback(0);
  }
}

// yields previous, places the next useful token in curr, skipping comments
static void internal_next() {
  _modp_callback(0);
  internal_next_comment();
}

static void internal_next_update(int type) {
  // TODO: we don't care about return type here
  prsr_update(type);
  _modp_callback(0);
  internal_next_comment();
}

static int consume_import_module_special() {
  if (td->cursor.type != TOKEN_STRING) {
    debugf("no string found in import/export\n");
    return ERROR__UNEXPECTED;
  }

  int len = td->cursor.len;
  if (len == 1 || (td->cursor.p[0] == '`' && td->cursor.p[len - 1] != '`')) {
    // nb. probably an error, but will just be treated as an expr
    return 0;
  }

  _modp_callback(SPECIAL__MODULE_PATH);
  internal_next_comment();
  return 0;
}

static int consume_import(int context) {
#ifdef DEBUG
  if (td->cursor.hash != LIT_IMPORT) {
    debugf("missing import keyword\n");
    return ERROR__UNEXPECTED;
  }
#endif
  internal_next_update(TOKEN_KEYWORD);

  if (td->cursor.type != TOKEN_STRING) {
    _check(consume_module_list(context));

    // consume "from"
    if (td->cursor.hash != LIT_FROM) {
      debugf("missing from keyword\n");
      return ERROR__UNEXPECTED;
    }
    internal_next_update(TOKEN_KEYWORD);
  }

  // match string (but not if `${}`)
  return consume_import_module_special();
}

static int consume_export(int context) {
#ifdef DEBUG
  if (td->cursor.hash != LIT_EXPORT) {
    debugf("missing export keyword\n");
    return ERROR__UNEXPECTED;
  }
#endif
  internal_next_update(TOKEN_KEYWORD);

  int is_default = 0;
  if (td->cursor.hash == LIT_DEFAULT) {
    internal_next_update(TOKEN_SYMBOL);
    is_default = 1;
  }

  // if this is class/function, consume with no value
  switch (td->cursor.hash) {
    case LIT_CLASS:
      return consume_class(context, SPECIAL__DECLARE);

    case LIT_ASYNC:
      if (!(prsr_peek() == TOKEN_LIT && prsr_peek_is_function())) {
        break;
      }
      // fall-through

    case LIT_FUNCTION:
      return consume_function(context, SPECIAL__DECLARE | SPECIAL__DECLARE_TOP);
  }

  // "export {..." or "export *" or "export default *"
  if ((!is_default && td->cursor.type == TOKEN_BRACE) || td->cursor.hash == MISC_STAR) {
    _check(consume_module_list(context));

    // consume optional "from"
    if (td->cursor.hash != LIT_FROM) {
      return 0;
    }
    internal_next_update(TOKEN_KEYWORD);

    // match string (but not if `${}`)
    return consume_import_module_special();
  }

  int has_decl = (td->cursor.hash & _MASK_DECL) ? 1 : 0;
  if (has_decl == is_default) {
    // can't default export "var foo", can't _not_ "export var".
    // TODO: should fail, allow for now
  }

  // otherwise, treat as expr (this catches var/let/const too)
  return consume_expr_compound(context);
}

inline static int consume_defn_name(int special) {
#ifdef DEBUG
  if (td->cursor.type != TOKEN_LIT) {
    debugf("expected name\n");
    return ERROR__UNEXPECTED;
  }
#endif
  if (special) {
    // this is a decl so the name is important
    prsr_update(TOKEN_SYMBOL);  // nb. should ban reserved words
    _modp_callback(special);
    internal_next_comment();
  } else {
    // otherwise, it's actually just a lit
    internal_next();
  }
  return 0;
}

// consumes "async function foo ()"
static int consume_function(int context, int special) {
  int statement_context = context;

  // check for leading async and update context
  if (td->cursor.hash == LIT_ASYNC) {
    statement_context |= CONTEXT__ASYNC;
    internal_next_update(TOKEN_KEYWORD);
  } else {
    statement_context &= ~(CONTEXT__ASYNC);
  }

  // expect function literal
  if (td->cursor.hash != LIT_FUNCTION) {
    debugf("missing 'function' keyword\n");
    return ERROR__UNEXPECTED;
  }
  internal_next_update(TOKEN_KEYWORD);

  // check for generator star
  if (td->cursor.hash == MISC_STAR) {
    internal_next();
  }

  // check for optional function name
  if (td->cursor.type == TOKEN_LIT) {
    _check(consume_defn_name(special));
  }

  // check for parens (nb. should be required)
  _check(consume_optional_arg_group(context));

  // consume function body
  return consume_statement(statement_context, SPECIAL__SCOPE);
}

static int is_arrowfunc_internal(int context) {
  if (td->cursor.hash == LIT_ASYNC) {
    internal_next_comment();
  }

  switch (td->cursor.type) {
  case TOKEN_LIT:
    if (td->cursor.hash == LIT_FUNCTION) {
      // found value "async function", not an arrowfunc
      return 0;
    }
    // arrow functions cannot be "async {x} =>", must be lit without paren
    internal_next_comment();
    if (td->cursor.hash == MISC_ARROW) {
      // "async foo =>" or "foo =>"
      return 1;
    }
    // "blah ???", ignore
    return 0;

  case TOKEN_PAREN:
    break;

  default:
    return 0;
  }

  internal_next_comment();  // move over TOKEN_PAREN

  for (;;) {
    // consume until we find close

    switch (td->cursor.type) {
      case TOKEN_CLOSE:
        prsr_peek();
        return prsr_peek_is_arrow();

      case TOKEN_LIT:
        // nb. might be unsupported (e.g. "this" or "import"), but invalid in this case
        internal_next_comment();
        break;

      case TOKEN_BRACE:
      case TOKEN_ARRAY:
        if (consume_destructuring(context, 0)) {
          return 0;  // error is not arrowfunc!
        }
        break;

      case TOKEN_OP:
        // nb. "async(,)" isn't really allowed but eh
        if (td->cursor.hash == MISC_COMMA) {
          internal_next_comment();
          continue;
        }
        return 0;

      default:
        return 0;
    }

    if (td->cursor.hash == MISC_EQUALS) {
      internal_next_comment();  // consume equals

      // right-side of equals can be a valid expr
      // TODO: if this doesn't move it's invalid/expr, which is a sign of not being arrowfunc
      if (consume_optional_expr(context)) {
        return 0;  // error is not arrowfunc!
      }
    }

    if (td->cursor.hash == MISC_COMMA || td->cursor.type == TOKEN_CLOSE) {
      continue;
    }
    return 0;
  }

  debugf("should not get here is_arrowfunc_internal\n");
  return ERROR__INTERNAL;
}

static int is_arrowfunc(int context) {
  // short-circuits
  if (td->cursor.type == TOKEN_LIT) {
    int peek = prsr_peek();
    if (td->cursor.hash != LIT_ASYNC) {
      // looks for "blah =>"
      return prsr_peek_is_arrow();
    } else if (peek == TOKEN_LIT) {
      return !prsr_peek_is_function();  // "async blah", always arrowfunc
    } else if (peek != TOKEN_PAREN) {
      return 0;  // "async ?", async use as e.g., op
    }
    // otherwise, continue below
  } else if (td->cursor.type != TOKEN_PAREN) {
    // TODO: this should really be a failure but whatever
    return 0;
  }

  // put this on stack so we don't have to actually allocate anything
  char *restore_resume = td->cursor.p;
  int restore_line_no = td->cursor.line_no;
  uint16_t restore_depth = td->depth;
  if (td->stack[td->depth - 1] == td->cursor.type) {
    --restore_depth;  // this token increased depth, just pop for restore
  }

  ++skip_context;  // reentrant so must ++

  int ret = is_arrowfunc_internal(context);

  --skip_context;

  td->resume = restore_resume;
  td->peek_at = restore_resume;
  td->line_no = restore_line_no;
  td->depth = restore_depth;
  td->cursor.len = 0;
  td->cursor.type = TOKEN_UNKNOWN;

  debugf("got is_arrowfunc=%d, resume=%c\n", ret, restore_resume[0]);
  prsr_next();
  if (td->cursor.type == TOKEN_COMMENT) {
    debugf("restore state ended on comment\n");
    return ERROR__INTERNAL;  // should never happen as we're pointing at value
  }

  return ret;
}

// we assume that we're pointing at one (is_arrowfunc has returned true)
static int consume_arrowfunc(int context) {
  int method_context = context;

  // "async" prefix
  if (td->cursor.hash == LIT_ASYNC) {
    method_context |= CONTEXT__ASYNC;
    internal_next_update(TOKEN_KEYWORD);
  } else {
    method_context &= ~(CONTEXT__ASYNC);
  }

  switch (td->cursor.type) {
    case TOKEN_LIT:
      prsr_update(TOKEN_SYMBOL);
      modp_callback(SPECIAL__DECLARE);
      internal_next_comment();
      break;

    case TOKEN_PAREN:
      internal_next();
      _check(consume_definition_list(context));

      if (td->cursor.type != TOKEN_CLOSE) {
        debugf("arrowfunc () did not end with close\n");
        return ERROR__UNEXPECTED;
      }
      internal_next();
      break;

    default:
      debugf("got unknown part of arrowfunc: %d\n", td->cursor.type);
      return ERROR__UNEXPECTED;
  }

  if (td->cursor.hash != MISC_ARROW) {
    debugf("arrowfunc missing =>\n");
    return ERROR__UNEXPECTED;
  }
  internal_next();  // consume =>

  if (td->cursor.type == TOKEN_BRACE) {
    _check(consume_statement(method_context, SPECIAL__SCOPE));
  }
  return consume_optional_expr(method_context);
}

static int consume_module_list(int context) {
  for (;;) {
    if (td->cursor.type == TOKEN_BRACE) {
      internal_next();
      _check(consume_module_list(context));
      if (td->cursor.type != TOKEN_CLOSE) {
        debugf("missing close after module list\n");
        return ERROR__UNEXPECTED;
      }
      internal_next();
    } else {
      // can start with "*", "foo", and end with "as blah"
      if (td->cursor.type == TOKEN_OP) {
        if (td->cursor.hash != MISC_STAR) {
          return 0;
        }
        internal_next();
      } else if (td->cursor.type == TOKEN_LIT) {
        // peek for "as x"
        // nb. this can be "as" or "from", this is gross
        if (prsr_peek() == TOKEN_LIT && prsr_peek_is_as()) {
          // this isn't a definition, but it's a property of the thing being imported
          _modp_callback(SPECIAL__PROPERTY);
          internal_next_comment();
        } else {
          prsr_update(TOKEN_SYMBOL);
          _modp_callback(SPECIAL__DECLARE | SPECIAL__DECLARE_TOP);
          internal_next_comment();
        }
      } else {
        return 0;
      }

      // catch optional "as x"
      if (td->cursor.hash == LIT_AS) {
        internal_next_update(TOKEN_KEYWORD);
        if (td->cursor.type != TOKEN_LIT) {
          debugf("missing literal after 'as'\n");
          return ERROR__UNEXPECTED;
        }
        prsr_update(TOKEN_SYMBOL);
        _modp_callback(SPECIAL__DECLARE | SPECIAL__DECLARE_TOP);
        internal_next_comment();
      }
    }

    if (td->cursor.hash != MISC_COMMA) {
      return 0;
    }
    internal_next();
  }
}

// consume var/const/let destructuring (not run for regular statement)
static int consume_destructuring(int context, int special) {
#ifdef DEBUG
  if (td->cursor.type != TOKEN_BRACE && td->cursor.type != TOKEN_ARRAY) {
    debugf("destructuring did not start with { or [\n");
    return ERROR__UNEXPECTED;
  }
#endif
  int start = td->cursor.type;
  internal_next();

  for (;;) {
    switch (td->cursor.type) {
      case TOKEN_CLOSE:
        internal_next();
        return 0;

      case TOKEN_LIT:
        // if this [foo] or {foo} without colon, announce now
        if (prsr_peek() == TOKEN_COLON) {
          _modp_callback(SPECIAL__PROPERTY);  // variable name is after colon
        } else {
          // e.g. "const {x} = ...", x is a symbol, decl and property
          prsr_update(TOKEN_SYMBOL);
          _modp_callback(special | SPECIAL__PROPERTY);
        }
        internal_next_comment();
        break;

      case TOKEN_ARRAY:
        if (start == TOKEN_BRACE) {
          // this is a computed property name
          _check(consume_expr_group(context));
          break;
        }
        _check(consume_destructuring(context, special));
        break;

      case TOKEN_BRACE:
        // nb. doesn't make sense in object context, but harmless
        _check(consume_destructuring(context, special));
        break;

      case TOKEN_OP:
        if (td->cursor.hash == MISC_COMMA) {
          // nb. solo comma
          internal_next();
          continue;
        }
        if (td->cursor.hash == MISC_SPREAD) {
          // this basically effects the next lit or destructured thing
          internal_next();
          continue;
        }
        // fall-through

      default:
        debugf("got unexpected inside object destructuring\n");
        return ERROR__UNEXPECTED;
    }

    // check for colon: blah
    if (td->cursor.type == TOKEN_COLON) {
      internal_next();

      switch (td->cursor.type) {
        case TOKEN_ARRAY:
        case TOKEN_BRACE:
          _check(consume_destructuring(context, special));
          break;

        case TOKEN_LIT:
          prsr_update(TOKEN_SYMBOL);
          _modp_callback(special);  // declaring new prop
          internal_next_comment();
          break;
      }
    }

    // consume default
    if (td->cursor.hash == MISC_EQUALS) {
      internal_next();
      _check(consume_optional_expr(context));
    }
  }
}

// consume (x=1, y=2) arg list
static int consume_optional_arg_group(int context) {
  if (td->cursor.type != TOKEN_PAREN) {
    return 0;
  }
  internal_next();
  _check(consume_definition_list(context));
  if (td->cursor.type != TOKEN_CLOSE) {
    debugf("arg_group did not finish with close\n");
    return ERROR__UNEXPECTED;
  }
  internal_next();
  return 0;
}

// consume list of definitions, i.e., on "var" etc (also allowed to be on first arg)
static int consume_definition_list(int context) {
  int special = SPECIAL__DECLARE | SPECIAL__DECLARE_TOP;
  if (td->cursor.hash == LIT_LET || td->cursor.hash == LIT_CONST) {
    special = SPECIAL__DECLARE;
  }
  if (td->cursor.hash & _MASK_DECL) {
    internal_next_update(TOKEN_KEYWORD);
  }

  for (;;) {
    switch (td->cursor.type) {
      case TOKEN_LIT:
        // nb. might be unsupported (e.g. "this" or "import"), but invalid in this case
        prsr_update(TOKEN_SYMBOL);
        _modp_callback(special);
        internal_next_comment();
        break;

      case TOKEN_BRACE:
      case TOKEN_ARRAY:
        _check(consume_destructuring(context, special));
        break;

      case TOKEN_OP:
        if (td->cursor.hash == MISC_COMMA) {
          internal_next();
          continue;
        }
        if (td->cursor.hash == MISC_SPREAD) {
          // nb. this doesn't make sense in var/const/lit, but is nonsensical
          // this basically effects the next lit or destructured thing
          internal_next();
          continue;
        }
        return 0;

      default:
        return 0;  // unhandled/unexpected
    }

    // FIXME: This is a little gross, since we handle this in both the definition list as well as
    // expr code. It's an argument to merge?
    switch (td->cursor.hash) {
      case LIT_IN:
      case LIT_OF:
        internal_next_update(TOKEN_OP);
        _check(consume_optional_expr(context));
        break;

      case MISC_EQUALS:
        internal_next();
        _check(consume_optional_expr(context));
        break;
    }

    if (td->cursor.hash != MISC_COMMA) {
      return 0;
    }
    internal_next();
  }
}

// consumes dict or class (allows either)
static int consume_dict(int context) {
  if (td->cursor.type != TOKEN_BRACE) {
    debugf("missing open brace for dict\n");
    return ERROR__UNEXPECTED;
  }
  internal_next();

  for (;;) {
    if (td->cursor.hash == MISC_SPREAD) {
      internal_next();
      _check(consume_optional_expr(context));
      continue;
    }

    // static prefix
    if (td->cursor.hash == LIT_STATIC && prsr_peek() != TOKEN_PAREN) {
      internal_next_update(TOKEN_KEYWORD);
    }

    // "async" prefix
    int method_context = context & ~(CONTEXT__ASYNC);
    if (td->cursor.hash == LIT_ASYNC) {
      int peek_type = prsr_peek();
      switch (peek_type) {
        case TOKEN_OP:
          if (td->peek_at[0] != '*') {
            // nb. could be '**' or '*=' but invalid/nonsensical
            break;
          }
          // fall-through

        case TOKEN_LIT:
          internal_next_update(TOKEN_KEYWORD);
          method_context |= CONTEXT__ASYNC;
          break;
      }
    }

    // generator
    if (td->cursor.hash == MISC_STAR) {
      internal_next();
    }

    // get/set without bracket
    if ((td->cursor.hash == LIT_GET || td->cursor.hash == LIT_SET) && prsr_peek() != TOKEN_PAREN) {
      internal_next_update(TOKEN_KEYWORD);
    }

    // name or bracketed name
    switch (td->cursor.type) {
      case TOKEN_LIT: {
        int is_symbol = 1;

        // if followed by : = or (, then this is a property
        switch (prsr_peek()) {
          case TOKEN_COLON:
          case TOKEN_PAREN:
            is_symbol = 0;
            break;

          case TOKEN_OP:
            if (td->peek_at[0] == '=' && td->peek_at[1] != '=') {
              is_symbol = 0;
            }
            break;
        }

        if (is_symbol) {
          prsr_update(TOKEN_SYMBOL);
        }
        _modp_callback(SPECIAL__PROPERTY);
        internal_next_comment();
        break;
      }

      case TOKEN_NUMBER:
        internal_next();
        break;

      case TOKEN_STRING:
        _check(consume_string(context));
        break;

      case TOKEN_ARRAY:
        _check(consume_expr_group(context));
        break;

      default:
        ;
        // ignore missing name, whatever
    }

    // check terminal case (which decides what type of entry this is), method or equal/colon
    switch (td->cursor.type) {
      case TOKEN_PAREN:
        // method
        _check(consume_expr_group(context));

        if (td->cursor.type != TOKEN_BRACE) {
          debugf("did not find brace after dict paren\n");
          return ERROR__UNEXPECTED;
        }
        _check(consume_statement(method_context, SPECIAL__SCOPE));
        break;

      case TOKEN_OP:
        if (td->cursor.hash != MISC_EQUALS) {
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
    switch (td->cursor.type) {
      case TOKEN_CLOSE:
        internal_next();
        return 0;

      case TOKEN_EOF:
        // don't stay here forever
        debugf("got EOF inside dict\n");
        return ERROR__UNEXPECTED;

      case TOKEN_OP:
        if (td->cursor.hash != MISC_COMMA) {
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

    debugf("unknown left-side dict part: %d\n", td->cursor.type);
    return ERROR__UNEXPECTED;
  }
}

// consumes expr if found (but might not move cursor if cannot)
static int consume_optional_expr(int context) {
  int value_line = 0;  // line_no of last value
  char *start = td->cursor.p;

#define _transition_to_value() { if (value_line) { return 0; } value_line = td->cursor.line_no; }
#define _seen_any (start != td->cursor.p)

  for (;;) {
    switch (td->cursor.type) {
      case TOKEN_SLASH:
        _check(prsr_update(value_line ? TOKEN_OP : TOKEN_REGEXP));
        continue;  // restart without move

      case TOKEN_BRACE:
        if (_seen_any && value_line) {
          // e.g. "(foo {})" is invalid, but "(foo + {})" is ok
          // we need this as "foo\n{}" must break out of this
          return 0;
        }
        _check(consume_dict(context));
        value_line = td->cursor.line_no;
        continue;

      case TOKEN_TERNARY:
        // nb. needs value on left (and contents!), but nonsensical otherwise
        _check(consume_expr_group(context));
        value_line = 0;
        continue;

      case TOKEN_PAREN:
        if (!value_line && is_arrowfunc(context)) {
          _check(consume_arrowfunc(context));
          value_line = td->cursor.line_no;
          break;
        }
        debugf("not arrowfunc, treating as group, cursor now=%d\n", td->cursor.type);
        // fall-through, regular group

      case TOKEN_ARRAY:
        _check(consume_expr_group(context));
        value_line = td->cursor.line_no;
        continue;

      case TOKEN_STRING:
        if (td->cursor.p[0] == '`' && value_line) {
          // tagged template, e.g. "hello`123`"
        } else {
          _transition_to_value();
        }
        _check(consume_string(context));
        continue;

      case TOKEN_SYMBOL:  // for calling again via async
      case TOKEN_REGEXP:
      case TOKEN_NUMBER:
        _transition_to_value();
        internal_next();
        continue;

      case TOKEN_LIT: {
        int type = TOKEN_SYMBOL;

        switch (td->cursor.hash) {
          case LIT_VAR:
          case LIT_LET:
          case LIT_CONST:
            if (_seen_any) {
              return 0;
            }
            // nb. only keyword at top-level and in for, but nonsensical otherwise
            // FIXME: move to top-level since we can call this externally
            _check(consume_definition_list(context));
            continue;

          case LIT_ASYNC:
            if (is_arrowfunc(context)) {
              _transition_to_value();
              _check(consume_arrowfunc(context));
              continue;
            }
            prsr_peek();
            if (prsr_peek_is_function()) {
              _transition_to_value();
              _check(consume_function(context, 0));
              continue;
            }
            break;  // not "async () =>" or "async function", treat as value

          case LIT_FUNCTION:
            _transition_to_value();
            _check(consume_function(context, 0));
            continue;

          case LIT_CLASS:
            _transition_to_value();
            _check(consume_class(context, 0));
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
            if (td->cursor.hash & (_MASK_REL_OP | _MASK_UNARY_OP)) {
              type = TOKEN_OP;
            } else if (td->cursor.hash & _MASK_KEYWORD) {
              // FIXME: this and below could be allowed to fall-through in more cases
              return 0;
            } else if (value_line) {
              return 0;  // value after value
            }

            // regular symbol
            // FIXME: if _seen_any=0, could be lvalue
        }

        if (type == TOKEN_SYMBOL && is_arrowfunc(context)) {
          _transition_to_value();
          _check(consume_arrowfunc(context));
          continue;
        }

        _check(prsr_update(type));
        continue;
      }

      case TOKEN_OP:
        if (td->cursor.hash & _MASK_UNARY_OP) {
          if (_seen_any && value_line) {
            // e.g., "var x = 123 new foo" is invalid
            return 0;
          }
        }

        switch (td->cursor.hash) {
          case MISC_COMMA:
            return 0;

          case MISC_NOT:
          case MISC_BITNOT:
            // nb. this matches _MASK_UNARY_OP above
            if (_seen_any && value_line) {
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
            if (td->cursor.type != TOKEN_LIT) {
              return 0;
            }
            _modp_callback(SPECIAL__PROPERTY);  // not a symbol
            internal_next_comment();
            value_line = td->cursor.line_no;
            continue;

          case MISC_INCDEC:
            if (!value_line) {
              // ok, attaches to upcoming
            } else if (td->cursor.line_no != value_line) {
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
    if (td->cursor.hash != MISC_COMMA) {
      break;
    }
    internal_next();
  }
  return 0;
}

static int consume_string(int context) {
#ifdef DEBUG
  if (td->cursor.type != TOKEN_STRING) {
    debugf("could not find string\n");
    return ERROR__UNEXPECTED;
  }
#endif
  internal_next();
  for (;;) {
    if (td->cursor.type != TOKEN_T_BRACE) {
      return 0;
    }
    _check(consume_expr_group(context));
#ifdef DEBUG
    if (td->cursor.type != TOKEN_STRING) {
      debugf("should see string again after tbrace/close\n");
      return ERROR__UNEXPECTED;
    }
#endif
    internal_next();  // this must be string
  }
  debugf("should not get here pointing at string\n");
  return ERROR__INTERNAL;  // should not get here
}

static int consume_expr_group(int context) {
  int start = td->cursor.type;
  switch (td->cursor.type) {
    case TOKEN_PAREN:
    case TOKEN_ARRAY:
    case TOKEN_TERNARY:
    case TOKEN_T_BRACE:
      break;

    default:
      debugf("expected expr group, was: %d\n", start);
      return ERROR__UNEXPECTED;
  }
  internal_next();

  for (;;) {
    _check(consume_expr_compound(context));

    // nb. not really good practice, but handles for-loop-likes
    if (td->cursor.type != TOKEN_SEMICOLON) {
      break;
    }

    internal_next();
  }

  if (td->cursor.type != TOKEN_CLOSE) {
    debugf("expected close after expr group\n");
    return ERROR__UNEXPECTED;
  }
  internal_next();
  return 0;
}

static int consume_class(int context, int special) {
#ifdef DEBUG
  if (td->cursor.hash != LIT_CLASS) {
    debugf("expected class keyword\n");
    return ERROR__UNEXPECTED;
  }
#endif
  internal_next_update(TOKEN_KEYWORD);

  if (td->cursor.type == TOKEN_LIT) {
    if (td->cursor.hash != LIT_EXTENDS) {
      _check(consume_defn_name(special));
    }
    if (td->cursor.hash == LIT_EXTENDS) {
      internal_next_update(TOKEN_KEYWORD);

      // nb. something must be here (but if it's not, that's an error, as we expect a '{' following)
      _check(consume_optional_expr(context));
    }
  }

  return consume_dict(context);
}

static int consume_statement(int context, int special_scope) {
  switch (td->cursor.type) {
    case TOKEN_EOF:
      return 0;

    case TOKEN_BRACE:
      _modp_callback(special_scope);  // marks for functions
      internal_next_comment();

      while (td->cursor.type != TOKEN_CLOSE) {
        int ret = consume_statement(context, 0);
        if (ret != 0) {
          return ret;
        } else if (td->cursor.type == TOKEN_EOF) {
          return ERROR__STACK;  // safety otherwise we won't leave for EOF
        }
      }

      _modp_callback(special_scope);  // mark close too
      internal_next_comment();
      return 0;

    case TOKEN_SEMICOLON:
      internal_next();  // consume
      return 0;

    case TOKEN_LIT:
      switch (td->cursor.hash) {
        case LIT_DEFAULT:
          internal_next_update(TOKEN_KEYWORD);
          if (td->cursor.type != TOKEN_COLON) {
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
          if (td->cursor.type != TOKEN_COLON) {
            debugf("no : after case\n");
            return ERROR__UNEXPECTED;
          }
          internal_next();
          return 0;

        case LIT_ASYNC: {
          if (prsr_peek() == TOKEN_LIT && prsr_peek_is_function()) {
            // only match "async function", as others are expr (e.g. "async () => {}")
            return consume_function(context, SPECIAL__DECLARE | SPECIAL__DECLARE_TOP);
          }
          // fall-through
        }
      }

      if (!(td->cursor.hash & _MASK_MASQUERADE) && prsr_peek() == TOKEN_COLON) {
        // nb. "await:" is invalid in async functions, but it's nonsensical anyway
        internal_next_update(TOKEN_LABEL);
        internal_next();  // consume TOKEN_COLON
        return 0;
      }

      break;
  }

  // match "if", "catch" etc
  if (td->cursor.hash & _MASK_CONTROL) {
    int control_hash = td->cursor.hash;
    td->cursor.type = TOKEN_KEYWORD;
    internal_next();

    // match "for" and "for await"
    if (control_hash == LIT_FOR) {
      if (td->cursor.hash == LIT_AWAIT) {
        td->cursor.type = TOKEN_KEYWORD;
        internal_next();
      }
    }

    if (td->cursor.type == TOKEN_PAREN) {
      _check(consume_expr_group(context));
    }
    // TODO: we could remove the following since we're not generating an AST.
    // ... it's not important that while(); is parsed as part of do-while.
    _check(consume_statement(context, 0));

    // special-case trailing "while(...)" for a 'do-while'
    if (control_hash == LIT_DO) {
      // TODO: in case the statement hasn't closed properly
      if (td->cursor.type == TOKEN_SEMICOLON) {
        internal_next();
      }

      if (td->cursor.hash != LIT_WHILE) {
        debugf("could not find while of do-while\n");
        return ERROR__UNEXPECTED;
      }
      internal_next_update(TOKEN_KEYWORD);

      if (td->cursor.type != TOKEN_PAREN) {
        debugf("could not find do-while parens\n");
        return ERROR__UNEXPECTED;
      }
      _check(consume_expr_group(context));
      // nb. ; is not required to trail "while (...)
    }

    return 0;
  }

  switch (td->cursor.hash) {
    case LIT_RETURN:
    case LIT_THROW: {
      int line_no = td->cursor.line_no;
      internal_next_update(TOKEN_KEYWORD);

      // "throw\n" is actually invalid, but we can't do anything about it
      if (line_no != td->cursor.line_no) {
        // TODO: could we detect "return" + expr and warn?
        return 0;
      }
      return consume_expr_compound(context);
      // nb. should look for semi here, just ignore
    }

    case LIT_IMPORT: {
      // if this is "import(" or "import.", treat as expr
      int type = prsr_peek();
      if (type == TOKEN_PAREN || (type == TOKEN_OP && td->peek_at[0] == '.' && td->peek_at[1] != '.')) {
        break;  // treat as expr
      }
      return consume_import(context);
    }

    case LIT_EXPORT:
      return consume_export(context);

    case LIT_CLASS:
      return consume_class(context, SPECIAL__DECLARE);

    case LIT_FUNCTION:
      return consume_function(context, SPECIAL__DECLARE | SPECIAL__DECLARE_TOP);

    case LIT_DEBUGGER: {
      int line_no = td->cursor.line_no;
      internal_next_update(TOKEN_KEYWORD);

      if (line_no != td->cursor.line_no) {
        return 0;
      }

      // nb. should look for semi here, ignore
      return 0;
    }

    case LIT_CONTINUE:
    case LIT_BREAK: {
      int line_no = td->cursor.line_no;
      internal_next_update(TOKEN_KEYWORD);
      if (line_no != td->cursor.line_no) {
        return 0;
      }

      // "break foo"
      if (td->cursor.type == TOKEN_LIT) {
        internal_next_update(TOKEN_LABEL);
        if (line_no != td->cursor.line_no) {
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
  skip_context = 0;

  if (td->cursor.type != TOKEN_COMMENT) {
    // n.b. it's possible but unlikely for this to fail (e.g. opens with "}")
    prsr_next();
  }

  return &(td->cursor);
}

int modp_run() {
  char *head = td->cursor.p;

  while (td->cursor.type == TOKEN_COMMENT) {
    _modp_callback(0);
    prsr_next();
  }
  _check(consume_statement(top_context, 0));

  int len = td->cursor.p - head;
  if (len == 0 && td->cursor.type != TOKEN_EOF) {
    debugf("expr did not get consumed, token=%d\n", td->cursor.type);
    return ERROR__UNEXPECTED;
  }
  return len;
}
