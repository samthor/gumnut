
#include "parser.h"
#include "lit.h"
#include "token.h"
#include <string.h>


#ifdef DEBUG
#include <stdio.h>
#define debugf(...) fprintf(stderr, "!!! " __VA_ARGS__); fprintf(stderr, "\n")
#else
#define debugf (void)sizeof
#endif

static int consume_statement();
static int consume_expr(int);
static int consume_expr_group();
static int consume_definition_group();
static int consume_function(int);
static int consume_class(int);
static int consume_expr_zero_many(int);
static int consume_expr_internal(int);


static int parser_skip = 0;

#define cursor (&(td->curr))
#define peek (&(td->peek))


// emit cursor and continue
static inline int cursor_next() {
  if (!parser_skip) {
    blep_parser_callback(cursor);
  }
  return blep_token_next();
}

// begins an optional stack (client can ignore it)
#define _STACK_BEGIN(type) { \
  int prev_parser_skip = parser_skip; \
  parser_skip = parser_skip || blep_parser_stack(type);

// ends an optional stack
#define _STACK_END() ; \
  parser_skip || blep_parser_stack(0); \
  parser_skip = prev_parser_skip; \
}

// ends an optional stack _and_ consumes an upcoming semicolon on same line
#define _STACK_END_SEMICOLON() \
    if (cursor->type == TOKEN_SEMICOLON && cursor->special == SPECIAL__SAMELINE) { \
      cursor_next(); /* only if on same line */ \
    } \
    _STACK_END();

#define _SET_RESTORE() \
  if (!parser_skip) { \
    ++parser_skip; \
    blep_token_set_restore();

#define _RESUME_RESTORE() \
    --parser_skip; \
    blep_token_restore(); \
  }

#define _check(v) { int _ret = v; if (_ret) { return _ret; }};

// consume a name of a function/class etc, needed as sometimes it's _just_ a name, not a decl
inline static int consume_defn_name(int special) {
  if (cursor->special == LIT_EXTENDS || cursor->type != TOKEN_LIT) {
#ifdef DEBUG
    if (peek->p) {
      debugf("expected empty peek location for empty emit");
      return ERROR__UNEXPECTED;
    }
#endif
    // emit empty symbol if a decl (move cursor => peek temporarily)
    if (special && !parser_skip) {
      memcpy(peek, cursor, sizeof(struct token));
      peek->vp = peek->p;  // no more void pointer for next token
      cursor->len = 0;
      cursor->special = SPECIAL__BASE | special;
      cursor->type = TOKEN_SYMBOL;
      cursor_next();
    }
    return 0;
  }

  if (special) {
    // this is a decl so the name is important
    cursor->type = TOKEN_SYMBOL;  // nb. should really ban reserved words
    cursor->special = SPECIAL__BASE | special;
    cursor_next();
  } else {
    // otherwise, it's actually just a lit
    cursor_next();
  }
  return 0;
}

static inline int consume_dict() {
#ifdef DEBUG
  if (cursor->type != TOKEN_BRACE) {
    debugf("missing open brace for dict\n");
    return ERROR__UNEXPECTED;
  }
#endif
  cursor_next();

  for (;;) {
    if (cursor->special == MISC_SPREAD) {
      cursor_next();
      _check(consume_expr(0));
      continue;
    }

    // static prefix
    if (cursor->special == LIT_STATIC && blep_token_peek() != TOKEN_PAREN) {
      cursor->type = TOKEN_KEYWORD;
      cursor_next();
    }

    // "async" prefix
    if (cursor->special == LIT_ASYNC) {
      int peek_type = blep_token_peek();
      switch (peek_type) {
        case TOKEN_OP:
          if (peek->special != MISC_STAR) {
            break;
          }
          // fall-through, only MISC_STAR and TOKEN_LIT is valid after LIT_ASYNC

        case TOKEN_KEYWORD:  // reentry
        case TOKEN_LIT:
          cursor->type = TOKEN_KEYWORD;  // "async" is keyword
          cursor_next();
          break;
      }
    }

    // generator
    if (cursor->special == MISC_STAR) {
      cursor_next();
    }

    // get/set without bracket
    if ((cursor->special == LIT_GET || cursor->special == LIT_SET) && blep_token_peek() != TOKEN_PAREN) {
      cursor->type = TOKEN_KEYWORD;
      cursor_next();
    }

    // name or bracketed name
    switch (cursor->type) {
      case TOKEN_SYMBOL:  // reentry
      case TOKEN_LIT: {
        // look for cases like `{foo}`, where foo is both a property and symbol
        int is_symbol = 1;

        // if followed by : = or (, then this is a property
        switch (blep_token_peek()) {
          case TOKEN_COLON:
          case TOKEN_PAREN:
            is_symbol = 0;
            break;

          case TOKEN_OP:
            if (peek->special == MISC_EQUALS) {
              is_symbol = 0;
            }
            break;
        }

        if (is_symbol) {
          cursor->type = TOKEN_SYMBOL;
        }
        cursor->special = SPECIAL__BASE | SPECIAL__PROPERTY;
        cursor_next();
        break;
      }

      case TOKEN_NUMBER:
        cursor_next();
        break;

      case TOKEN_STRING:
        if (cursor->p[0] == '`' && cursor->p[cursor->len - 1] != '`') {
          // can't have templated string here at all, but allow single
          return ERROR__UNEXPECTED;
        }
        cursor_next();
        break;

      case TOKEN_ARRAY:
        _check(consume_expr_group());
        break;

      default:
        ;
        // ignore missing name, whatever
    }

    // check terminal case (which decides what type of entry this is), method or equal/colon
    switch (cursor->type) {
      case TOKEN_PAREN:
        // method
        _STACK_BEGIN(STACK__FUNCTION);
        _check(consume_definition_group());
        _check(consume_statement());
        _STACK_END();
        break;

      case TOKEN_OP:
        if (cursor->special != MISC_EQUALS) {
          break;
        }
        // fall-through

      case TOKEN_COLON:
        // nb. this allows "async * foo:" or "async foo =" which is nonsensical
        cursor_next();
        _check(consume_expr(0));
        break;
    }

    // handle tail cases (close, eof, op, etc)
    switch (cursor->type) {
      case TOKEN_CLOSE:
        cursor_next();
        return 0;

      case TOKEN_EOF:
        // don't stay here forever
        debugf("got EOF inside dict\n");
        return ERROR__UNEXPECTED;

      case TOKEN_OP:
        if (cursor->special != MISC_COMMA) {
          break;
        }
        cursor_next();
        continue;

      case TOKEN_SEMICOLON:
        cursor_next();
        continue;

      case TOKEN_LIT:
      case TOKEN_NUMBER:
      case TOKEN_STRING:
      case TOKEN_ARRAY:
        continue;
    }

    debugf("unknown left-side dict part: %d\n", cursor->type);
    return ERROR__UNEXPECTED;
  }
}

// consume zero or many expressions (which can also be blank), separated by commas
// may consume literally nothing
static int consume_expr_zero_many(int is_statement) {
  for (;;) {
    _check(consume_expr_internal(is_statement));
    if (cursor->special != MISC_COMMA) {
      break;
    }
    cursor_next();
  }

  return 0;
}

// consumes a boring grouped expr (paren, array, ternary)
static int consume_expr_group() {
  int open = cursor->type;
#ifdef DEBUG
  switch (cursor->type) {
    case TOKEN_PAREN:
    case TOKEN_ARRAY:
    case TOKEN_TERNARY:
      break;

    default:
      debugf("expected expr group, was: %d", cursor->type);
      return ERROR__UNEXPECTED;
  }
#endif
  cursor_next();
  _check(consume_expr_zero_many(0));

  if (cursor->type != TOKEN_CLOSE) {
    debugf("expected close for expr group (got %d), open was: %d", cursor->type, open);
    return ERROR__UNEXPECTED;
  }
  cursor_next();  // consuming close
  return 0;
}

// consume arrowfunc from and including "=>"
static int consume_arrowfunc_from_arrow() {
  if (cursor->special != MISC_ARROW) {
    debugf("arrowfunc missing =>\n");
    return ERROR__UNEXPECTED;
  }
  cursor_next();  // consume =>

  if (cursor->type == TOKEN_BRACE) {
    return consume_statement();
  } else {
    return consume_expr(0);  // TODO: if statement we might want to pass this through
  }
}

// we assume that we're pointing at one (is_arrowfunc has returned true)
static int consume_arrowfunc() {
  _STACK_BEGIN(STACK__FUNCTION);

  // "async" prefix without immediate =>
  if (cursor->special == LIT_ASYNC && !(blep_token_peek() == TOKEN_OP && peek->special == MISC_ARROW)) {
    cursor->type = TOKEN_KEYWORD;
    cursor_next();
  }

  switch (cursor->type) {
    case TOKEN_LIT:
      cursor->type = TOKEN_SYMBOL;
      cursor->special = SPECIAL__BASE | SPECIAL__DECLARE;
      cursor_next();
      break;

    case TOKEN_PAREN:
      cursor_next();
      _check(consume_definition_group());
      break;

    default:
      debugf("got unknown part of arrowfunc: %d\n", cursor->type);
      return ERROR__UNEXPECTED;
  }

  _check(consume_arrowfunc_from_arrow());
  _STACK_END();
  return 0;
}

static int consume_template_string() {
#ifdef DEBUG
  if (cursor->type != TOKEN_STRING || cursor->p[0] != '`') {
    debugf("bad templated string");
    return ERROR__UNEXPECTED;
  }
#endif

  for (;;) {
    char end = cursor->p[cursor->len - 1];
    cursor_next();

    if (end == '`') {
      return 0;
    } else if (end != '{') {
      // we don't have to check for "${", the only case where it won't exist if the file ends with:
      //   `foo{
      // ... in which case consume_expr() below fails
      debugf("templated string didn't end with ` or ${");
      return ERROR__UNEXPECTED;
    }

    _check(consume_expr(0));

    if (cursor->type != TOKEN_STRING || cursor->p[0] != '}') {
      debugf("templated string didn't restart with }, was %d", cursor->type);
      return ERROR__UNEXPECTED;
    }
  }
}

static int is_token_assign_like(struct token *t) {
  if (t->special == MISC_EQUALS) {
    return 1;
  }
  int len = t->len;
  return len >= 2 && t->p[len - 1] == '=' && t->p[len - 2] != '=';
}

// like the other, but counts ()'s
static int consume_expr_internal(int is_statement) {
  int paren_count = 0;

restart_expr:
  (void)sizeof(0);
  int value_line = 0;
  char *start = cursor->p;

  // TODO: at this point we look for () => {} or destructuring, only valid here

#define _maybe_abandon() { if (is_statement && !paren_count) { return 0; } }
#define _transition_to_value() { if (value_line) { _maybe_abandon(); } value_line = cursor->line_no; }

  for (;;) {
    // 1st step: replace stuff with their intended types and continue
    switch (cursor->type) {
      case TOKEN_OP:
        if (!value_line && cursor->p[0] == '/') {
          blep_token_update(TOKEN_REGEXP);  // we got it wrong
        } else if (cursor->special == MISC_NOT || cursor->special == MISC_BITNOT) {
          // TODO: just fixes a failure in the tokenizer's hashes
          cursor->special = _MASK_UNARY_OP;
        }
        break;

      case TOKEN_REGEXP:
        if (value_line) {
          blep_token_update(TOKEN_OP);  // we got it wrong
        }
        break;

      case TOKEN_LIT:
        cursor->type = TOKEN_SYMBOL;  // this is a symbol _unless_...

        switch (cursor->special) {
          case LIT_ASYNC:
            // we check for arrowfunc at head, so this must be symbol or "async function"
            blep_token_peek();
            if (peek->special == LIT_FUNCTION) {
              cursor->type = TOKEN_KEYWORD;
            }
            break;

          case LIT_CLASS:
          case LIT_FUNCTION:
            cursor->type = TOKEN_KEYWORD;
            break;

          case LIT_OF:
            if (value_line && !is_statement) {
              // this isn't really valid in most exprs
              cursor->type = TOKEN_OP;
            }
            break;

          default:
            if (cursor->special & (_MASK_UNARY_OP | _MASK_REL_OP)) {
              cursor->type = TOKEN_OP;
            } else if (cursor->special & (_MASK_KEYWORD | _MASK_STRICT_KEYWORD)) {
              _maybe_abandon();
              cursor->type = TOKEN_KEYWORD;
            }
        }
    }

    // 2nd step: process normal stuff
    switch (cursor->type) {
      case TOKEN_KEYWORD:
        _transition_to_value();

        switch (cursor->special) {
          case LIT_ASYNC:
          case LIT_FUNCTION:
            _check(consume_function(0));
            break;

          case LIT_CLASS:
            _check(consume_class(0));
            break;
        }
        continue;

      case TOKEN_BRACE:
        _transition_to_value();
        _check(consume_dict());
        continue;

      case TOKEN_TERNARY:
        // nb. needs value on left (and contents!), but nonsensical otherwise
        _check(consume_expr_group());
        value_line = 0;
        continue;

      case TOKEN_PAREN:
        if (value_line) {
          // this is a function call
          _check(consume_expr_group());
          value_line = cursor->line_no;
          continue;
        }
        ++paren_count;
        cursor_next();

        // if we see a TOKEN_LIT immediately after us, see if it's actually a paren'ed lvalue
        // (this is incredibly uncommon, don't do this, e.g.: `(x)++`)
        if (cursor->type != TOKEN_LIT || cursor->special & (_MASK_KEYWORD | _MASK_STRICT_KEYWORD) || blep_token_peek() != TOKEN_CLOSE) {
          continue;
        }

        int is_lvalue = 0;
        _SET_RESTORE();

        int paren_remain = paren_count;
        do {
          cursor_next();
          blep_token_peek();
          --paren_remain;
        } while (peek->type == TOKEN_CLOSE && paren_remain);

        blep_token_peek();
        is_lvalue = is_token_assign_like(peek) || peek->special == MISC_INCDEC;
        _RESUME_RESTORE();

        cursor->type = TOKEN_SYMBOL;
        cursor->special = is_lvalue ? SPECIAL__BASE | SPECIAL__CHANGE : 0;
        cursor_next();
        // parens will be caught next loop
        continue;

      case TOKEN_ARRAY:
        _check(consume_expr_group());
        value_line = cursor->line_no;
        continue;

      case TOKEN_CLOSE:
        if (paren_count) {
          --paren_count;
          cursor_next();
          continue;
        }
        return 0;

      case TOKEN_STRING:
        if (cursor->p[0] == '`') {
          _check(consume_template_string())
          value_line = cursor->line_no;
        } else {
          _transition_to_value();
          cursor_next();
        }
        continue;

      case TOKEN_SYMBOL:
        _transition_to_value();
        cursor->special = 0;  // nothing special about symbols

        blep_token_peek();
        if (is_token_assign_like(peek) || peek->special == MISC_INCDEC) {
          cursor->special = SPECIAL__BASE | SPECIAL__CHANGE;
        }

        cursor_next();
        continue;

      case TOKEN_NUMBER:
      case TOKEN_REGEXP:
        _transition_to_value();
        cursor_next();
        continue;

      case TOKEN_OP:
        break;  // below

      default:
        return 0;
    }
#ifdef DEBUG
    if (cursor->type != TOKEN_OP) {
      debugf("non-op fell through");
      return ERROR__INTERNAL;
    }
#endif


    // 3rd step: handle TOKEN_OP

    if (cursor->special & _MASK_UNARY_OP) {
      if (start != cursor->p && value_line) {
        // e.g., "var x = 123 new foo", "a ~ 2", "b ! c" is invalid
        _maybe_abandon();
      }
      cursor_next();
      value_line = 0;
      continue;
    } else if (is_token_assign_like(cursor)) {
      // nb. special-case for = as we allow arrowfunc after it
      cursor_next();
      goto restart_expr;
    }

    switch (cursor->special) {
      case MISC_COMMA:
        if (paren_count) {
          cursor_next();
          goto restart_expr;
        }
        return 0;

      case MISC_CHAIN:
      case MISC_DOT:
        if (!value_line) {
          _maybe_abandon();
        }
        cursor_next();

        // technically chain only allows e.g, ?.foo, ?.['foo'], or ?.(arg)
        // but broadly means "value but only continue if non-null"
        if (cursor->type == TOKEN_PAREN || cursor->type == TOKEN_ARRAY) {
          cursor_next();
          value_line = cursor->line_no;
          continue;
        } else if (cursor->type != TOKEN_LIT) {
          debugf("got dot/chain with unknown after: %d", cursor->type);
          return ERROR__UNEXPECTED;
        }
        cursor->special = 0;
        cursor_next();
        continue;

      case MISC_INCDEC:
        if (value_line) {
          if (cursor->line_no != value_line) {
            _maybe_abandon();  // not attached to previous, avoid consuming
          }
          cursor_next();  // this is not attached to a simple lvalue
          continue;
        }

        int paren_count_here = 0;

        // otherwise, look for upcoming lvalue
        cursor_next();
        while (cursor->type == TOKEN_PAREN) {
          ++paren_count_here;
          cursor_next();
        }
        paren_count += paren_count_here;
        if (cursor->type != TOKEN_LIT) {
          continue;  // e.g. `++((1`, ignore
        }

        blep_token_peek();
        if (peek->type == TOKEN_CLOSE) {
          // easy, e.g. `++(((x)` or `++x)`, we don't care if we're missing further )'s, invalid anyway
        } else if (paren_count_here) {
          // do nothing: either invalid or long, e.g. `++(x + 1)` or `++(x.y)` or ++(x().y)
          continue;
        } else if (peek->special == MISC_DOT || peek->special == MISC_CHAIN || peek->type == TOKEN_PAREN || peek->type == TOKEN_ARRAY) {
          // do nothing: run-on to something else, e.g. ++foo().bar
          // nb. MISC_CHAIN isn't supported technically
          continue;
        }

        cursor->special = SPECIAL__BASE | SPECIAL__CHANGE;
        value_line = cursor->line_no;
        cursor_next();
        continue;

      default:
        // all other ops are fine
        value_line = 0;
        cursor_next();
    }
  }

#undef _maybe_abandon
#undef _transition_to_value
}

static inline int consume_expr(int is_statement) {
  char *start = cursor->p;
  _check(consume_expr_internal(is_statement));

  if (start == cursor->p) {
    debugf("could not consume expr, was: %d", cursor->type);
    return ERROR__UNEXPECTED;
  }
  return 0;
}

// consume destructuring: this is not always __DECLARE, because it could be in an expr
// special will contain SPECIAL__TOP or SPECIAL__DECLARE
static int consume_destructuring(int special) {
#ifdef DEBUG
  int special_mask = (SPECIAL__TOP | SPECIAL__DECLARE);
  if ((special | special_mask) != special_mask) {
    debugf("destrucuring special should only be TOP | DECLARE, was %d", special);
    return ERROR__INTERNAL;
  }
  if (cursor->type != TOKEN_BRACE && cursor->type != TOKEN_ARRAY) {
    debugf("destructuring did not start with { or [");
    return ERROR__UNEXPECTED;
  }
#endif
  int start = cursor->type;
  cursor_next();

  for (;;) {
    switch (cursor->type) {
      case TOKEN_CLOSE:
        cursor_next();
        return 0;

      case TOKEN_SYMBOL:  // reentry
      case TOKEN_LIT: {
        // if this [foo] or {foo} without colon, announce now
        if (blep_token_peek() == TOKEN_COLON) {
          // variable name after colon
          cursor->special = SPECIAL__BASE | SPECIAL__PROPERTY;
        } else {
          // e.g. "const {x} = ...", x is a symbol, decl and property
          cursor->type = TOKEN_SYMBOL;
          cursor->special = SPECIAL__BASE | SPECIAL__PROPERTY | SPECIAL__CHANGE | special;
        }
        cursor_next();
        break;
      }

      case TOKEN_ARRAY:
        if (start == TOKEN_BRACE) {
          // this is a computed property name
          _check(consume_expr(0));
          break;
        }
        _check(consume_destructuring(special));
        break;

      case TOKEN_BRACE:
        // nb. doesn't make sense in object context, but harmless
        _check(consume_destructuring(special));
        break;

      case TOKEN_OP:
        if (cursor->special == MISC_COMMA) {
          // nb. solo comma
          cursor_next();
          continue;
        }
        if (cursor->special == MISC_SPREAD) {
          // this basically effects the next lit or destructured thing
          cursor_next();
          continue;
        }
        // fall-through

      default:
        debugf("got unexpected inside object destructuring");
        return ERROR__UNEXPECTED;
    }

    // check for colon: blah
    if (cursor->type == TOKEN_COLON) {
      cursor_next();

      switch (cursor->type) {
        case TOKEN_ARRAY:
        case TOKEN_BRACE:
          _check(consume_destructuring(special));
          break;

        case TOKEN_SYMBOL:  // reentry
        case TOKEN_LIT:
          cursor->type = TOKEN_SYMBOL;
          cursor->special = SPECIAL__BASE | SPECIAL__CHANGE | special;
          cursor_next();
          break;
      }
    }

    // consume default
    if (cursor->special == MISC_EQUALS) {
      cursor_next();
      _check(consume_expr(0));
    }
  }
}

// consumes a single definition (e.g. `catch (x)` or x in `function(x, y) {}`
static int consume_optional_definition(int special, int is_statement) {
  int is_spread = 0;
  int is_assign = 0;

  // this only applies to functions
  if (cursor->special == MISC_SPREAD) {
    cursor_next();
    is_spread = 1;
  }

  switch (cursor->type) {
    case TOKEN_SYMBOL:  // reentry
    case TOKEN_LIT:
      // nb. might be unsupported (e.g. "this" or "import"), but invalid in this case
      cursor->type = TOKEN_SYMBOL;
      cursor->special = SPECIAL__BASE | SPECIAL__DECLARE | special;

      // look for assignment, incredibly likely, but check anyway
      blep_token_peek();
      switch (peek->special) {
        case LIT_IN:
        case LIT_OF:
        case MISC_EQUALS:
          cursor->special |= SPECIAL__CHANGE;
      }
      cursor_next();
      break;

    case TOKEN_BRACE:
    case TOKEN_ARRAY:
      _check(consume_destructuring(special | SPECIAL__DECLARE));
      break;

    default:
      if (is_spread) {
        debugf("destructuring had bad spread");
        return ERROR__UNEXPECTED;
      }
      return 0;  // can't consume this
  }

  return 0;
}

// consumes an optional "= <expr>"
static int consume_optional_assign_suffix(int is_statement) {
  if (cursor->special == MISC_EQUALS) {
    cursor_next();
    _check(consume_expr(is_statement));
  }
  return 0;
}

// consumes a number of comma-separated definitions
static int consume_definition_list(int special, int is_statement) {
  for (;;) {
    _check(consume_optional_definition(special, is_statement));
    _check(consume_optional_assign_suffix(is_statement));
    if (cursor->special != MISC_COMMA) {
      return 0;
    }
    cursor_next();
  }
}

// wraps consume_definition_list (comma-separated list) by looking for parens
// used in functions (normal, class, arrow)
static int consume_definition_group() {
  if (cursor->type != TOKEN_PAREN) {
    debugf("arg_group didn't start with paren");
    return ERROR__UNEXPECTED;
  }
  cursor_next();

  _check(consume_definition_list(SPECIAL__TOP, 0));
  if (cursor->type != TOKEN_CLOSE) {
    debugf("arg_group did not finish with close");
    return ERROR__UNEXPECTED;
  }
  cursor_next();
  return 0;
}

static int consume_function(int special) {
  _STACK_BEGIN(STACK__FUNCTION);

  int is_async = 0;
  int is_generator = 0;

  if (cursor->special == LIT_ASYNC) {
    is_async = 1;
    cursor->type = TOKEN_KEYWORD;
    cursor_next();
  }

  if (cursor->special != LIT_FUNCTION) {
    debugf("function did not start with 'function'");
    return ERROR__UNEXPECTED;
  }
  cursor->type = TOKEN_KEYWORD;
  cursor_next();

  if (cursor->special == MISC_STAR) {
    is_generator = 1;
    cursor_next();
  }

  _check(consume_defn_name(special));
  debugf("function, generator=%d async=%d", is_generator, is_async);

  _check(consume_definition_group());
  _check(consume_statement());

  _STACK_END();
  return 0;
}

static int consume_class(int special) {
  _STACK_BEGIN(STACK__CLASS);
#ifdef DEBUG
  if (cursor->special != LIT_CLASS) {
    debugf("expected class keyword\n");
    return ERROR__UNEXPECTED;
  }
#endif
  cursor->type = TOKEN_KEYWORD;
  cursor_next();

  _check(consume_defn_name(special));

  if (cursor->special == LIT_EXTENDS) {
    cursor->type = TOKEN_KEYWORD;
    cursor_next();

    // nb. something must be here (but if it's not, that's an error, as we expect a '{' following)
    _STACK_BEGIN(STACK__EXPR);
    _check(consume_expr(0));
    _STACK_END();
  }

  _check(consume_dict());
  _STACK_END();
  return 0;
}

static inline int consume_control_group_inner(int control_hash) {
  switch (control_hash) {
    case LIT_CATCH:
      // special-case catch, which creates a local scoped var
      return consume_optional_definition(0, 0);

    case LIT_FOR:
      break;

    default:
      return consume_expr_zero_many(0);
  }

  if (cursor->type == TOKEN_SEMICOLON) {
    // fine, ignore left block
  } else {
    if (cursor->special & _MASK_DECL) {
      // started with "var" etc
      int special = cursor->special == LIT_VAR ? SPECIAL__TOP : 0;
      cursor->type = TOKEN_KEYWORD;
      cursor_next();

      char *start = cursor->p;
      _check(consume_optional_definition(special, 0));
      if (start == cursor->p) {
        debugf("expected var def after decl");
        return ERROR__UNEXPECTED;
      }

      // `for (var x of y)` or `for (var {x,y} of z)`
      if (cursor->special == LIT_OF || cursor->special == LIT_IN) {
        cursor->type = TOKEN_KEYWORD;
        cursor_next();
        return consume_expr(0);
      }

      // otherwise, this is a ;; loop and can be a normal decl
      // step past optional "= 1" and "," then continue more definitions
      _check(consume_optional_assign_suffix(0));
      if (cursor->special == MISC_COMMA) {
        cursor_next();
        _check(consume_definition_list(special, 0));
      }
    } else {
      // otherwise, this is an expr
      // ... it allows "is" and "of" to be mapped to keywords
      _check(consume_expr_zero_many(0));
    }
  }

  // after left block, check for semicolon
  if (cursor->type != TOKEN_SEMICOLON) {
    return 0;  // not always valid, but just allow it anyway
  }
  cursor_next();

  // consume middle block (skip if semicolon)
  _check(consume_expr_zero_many(0));
  if (cursor->type != TOKEN_SEMICOLON) {
    debugf("expected 2nd semicolon");
    return ERROR__UNEXPECTED;
  }
  cursor_next();

  // consume right block (skip if close)
  if (cursor->type == TOKEN_CLOSE) {
    return 0;
  }
  return consume_expr_zero_many(0);
}

static int consume_control() {
#ifdef DEBUG
  if (!(cursor->special & _MASK_CONTROL)) {
    debugf("expected _MASK_CONTROL for consume_control");
    return ERROR__INTERNAL;
  }
#endif

  int control_hash = cursor->special;

  _STACK_BEGIN(STACK__CONTROL);
  cursor->type = TOKEN_KEYWORD;
  cursor_next();

  // match "for" and "for await"
  if (control_hash == LIT_FOR) {
    if (cursor->special == LIT_AWAIT) {
      cursor->type = TOKEN_KEYWORD;
      cursor_next();
    }
  }

  // match inner parens of control
  if (cursor->type == TOKEN_PAREN) {
    cursor_next();
    _check(consume_control_group_inner(control_hash));
    if (cursor->type != TOKEN_CLOSE) {
      debugf("could not find closer of control ()");
      return ERROR__UNEXPECTED;
    }
    cursor_next();
  }

  // consume inner statement
  _check(consume_statement());

  // special-case trailing "while(...)" for a 'do-while'
  if (control_hash == LIT_DO) {

    if (cursor->special != LIT_WHILE) {
      debugf("could not find while of do-while");
      return ERROR__UNEXPECTED;
    }
    cursor->type = TOKEN_KEYWORD;
    cursor_next();

    if (cursor->type != TOKEN_PAREN) {
      debugf("could not find paren for while");
      return ERROR__UNEXPECTED;
    }

    // this isn't special (can't define var/let etc), just consume as expr on paren
    _check(consume_expr_group());
  }

  _STACK_END();
  return 0;
}

static int consume_expr_statement() {
  _STACK_BEGIN(STACK__EXPR);

  for (;;) {
    _check(consume_expr(1));
    if (cursor->special != MISC_COMMA) {
      break;
    }
    cursor_next();
  }

  _STACK_END_SEMICOLON();
  return 0;
}

static int consume_statement() {
  debugf("consume_statement: %d (%.*s %d)", cursor->type, cursor->len, cursor->p, cursor->special);

  switch (cursor->type) {
    case TOKEN_EOF:
    case TOKEN_COLON:
      debugf("got EOF/COLON for statement");
      return ERROR__UNEXPECTED;

    case TOKEN_CLOSE:
      return 0;

    case TOKEN_BRACE:
      _STACK_BEGIN(STACK__BLOCK);
      cursor_next();

      do {
        debugf("consuming block statement");
        _check(consume_statement());
      } while (cursor->type != TOKEN_CLOSE);

      cursor_next();
      _STACK_END();
      return 0;

    case TOKEN_SEMICOLON:
      _STACK_BEGIN(STACK__MISC);
      cursor_next();
      _STACK_END();
      return 0;

    case TOKEN_LABEL:  // reentry
      _STACK_BEGIN(STACK__LABEL);
      cursor_next();

      if (cursor->type != TOKEN_COLON) {
        return ERROR__UNEXPECTED;
      }
      cursor_next();
      _check(consume_statement());

      _STACK_END();
      return 0;

    case TOKEN_KEYWORD:  // reentry
    case TOKEN_SYMBOL:   // reentry
    case TOKEN_LIT:
      break;

    default:
      return consume_expr_statement();
  }

  switch (cursor->special) {
    case LIT_DEFAULT:
      _STACK_BEGIN(STACK__LABEL);

      cursor->type = TOKEN_KEYWORD;
      cursor_next();

      if (cursor->type != TOKEN_COLON) {
        debugf("no : after default");
        return ERROR__UNEXPECTED;
      }
      cursor_next();

      _STACK_END();
      break;

    case LIT_CASE:
      _STACK_BEGIN(STACK__LABEL);

      cursor->type = TOKEN_KEYWORD;
      cursor_next();

      _check(consume_expr(0));

      if (cursor->type != TOKEN_COLON) {
        debugf("no : after case");
        return ERROR__UNEXPECTED;
      }
      cursor_next();

      _STACK_END();
      break;

    case LIT_ASYNC:
      blep_token_peek();
      if (peek->special != LIT_FUNCTION) {
        break;  // only "async function" is a top-level function
      }
      // fall-through

    case LIT_FUNCTION:
      return consume_function(SPECIAL__DECLARE | SPECIAL__TOP);

    case LIT_CLASS:
      return consume_class(SPECIAL__DECLARE);

    case LIT_RETURN:
    case LIT_THROW:
      _STACK_BEGIN(STACK__MISC);
      int line_no = cursor->line_no;

      cursor->type = TOKEN_KEYWORD;
      cursor_next();

      if (line_no == cursor->line_no) {
        _check(consume_expr(1));
      }

      _STACK_END_SEMICOLON();
      return 0;

    case LIT_DEBUGGER:
      _STACK_BEGIN(STACK__MISC);

      cursor->type = TOKEN_KEYWORD;
      cursor_next();

      _STACK_END_SEMICOLON();
      return 0;

    case LIT_CONTINUE:
    case LIT_BREAK:
      _STACK_BEGIN(STACK__MISC);
      int line_no = cursor->line_no;

      cursor->type = TOKEN_KEYWORD;
      cursor_next();

      if (line_no == cursor->line_no) {
        if (cursor->type == TOKEN_LIT) {
          cursor->type = TOKEN_LABEL;
          cursor_next();
        }
      }

      _STACK_END_SEMICOLON();
      return 0;

    case LIT_IMPORT:
    case LIT_EXPORT:
      return ERROR__TODO;
  }

  if (!(cursor->special & _MASK_MASQUERADE)) {
    if (blep_token_peek() == TOKEN_COLON) {
      // nb. "await:" is invalid in async functions, but it's nonsensical anyway
      // we restart this function to parse as label
      cursor->type = TOKEN_LABEL;
      return consume_statement();
    }
  }

  if (cursor->special & _MASK_CONTROL) {
    return consume_control();
  } else if (cursor->special & _MASK_DECL) {
    _STACK_BEGIN(STACK__DECLARE);
    int special = cursor->special == LIT_VAR ? SPECIAL__TOP : 0;
    cursor->type = TOKEN_KEYWORD;
    cursor_next();
    _check(consume_definition_list(special, 1));
    _STACK_END_SEMICOLON();
    return 0;
  } else if (cursor->special & _MASK_UNARY_OP || !cursor->special) {
    return consume_expr_statement();
  }

  // catches things like "enum", "protected", which are keywords but largely unhandled
  if (cursor->special & (_MASK_KEYWORD | _MASK_STRICT_KEYWORD)) {
    debugf("got fallback TOKEN_KEYWORD");
    _STACK_BEGIN(STACK__MISC);
    cursor->type = TOKEN_KEYWORD;
    cursor_next();
    _STACK_END_SEMICOLON();
    return 0;
  }

  return consume_expr_statement();
}

void blep_parser_init() {
  blep_token_next();
  debugf("zero cursor: type=%d", cursor->type);
}

int blep_parser_run() {
  if (cursor->type == TOKEN_EOF) {
    return 0;
  }
  char *head = cursor->p;

  _check(consume_statement());

  int len = cursor->p - head;
  if (len == 0 && cursor->type != TOKEN_EOF) {
    debugf("blep_parser_run consumed nothing, token=%d", cursor->type);
    return ERROR__UNEXPECTED;
  }
  return len;
}