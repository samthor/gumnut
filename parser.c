
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
static int consume_expr();
static int consume_expr_group();
static int consume_definition_group();


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
    // emit empty symbol (move cursor => peek temporarily)
    if (!parser_skip) {
      memcpy(peek, cursor, sizeof(struct token));
      peek->vlen = 0;
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
      _check(consume_expr());
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
        _check(consume_expr());
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
  }}

// consumes a compound expr separated by ,'s
static int consume_expr_compound() {
  for (;;) {
    _check(consume_expr());
    if (cursor->special != MISC_COMMA) {
      break;
    }
    cursor_next();
  }
  return 0;
}

// consumes a boring grouped expr (paren, array, ternary)
static int consume_expr_group() {
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

  _check(consume_expr_compound());

  if (cursor->type != TOKEN_CLOSE) {
    debugf("expected close after expr group");
    return ERROR__UNEXPECTED;
  }
  return cursor_next();
}

static int consume_optional_expr() {
  int value_line = 0;

  // TODO: check for arrowfunc

#define _transition_to_value() { if (value_line) { return 0; } value_line = cursor->line_no; }

  for (;;) {
    switch (cursor->type) {
      // case TOKEN_OP:
      //   if (value_line) {
      //     value_line = 0;
      //     continue;
      //     // we got it wrong, reparse as REGEXP
      //   }
      //   continue;

      case TOKEN_REGEXP:
        if (!value_line) {
          value_line = 1;
          continue;
        }
        // we got it wrong, reparse as OP
        return ERROR__TODO;

      case TOKEN_SYMBOL:  // reentry
      case TOKEN_NUMBER:
        _transition_to_value();
        cursor_next();
        continue;

      case TOKEN_BRACE:
        _transition_to_value();
        _check(consume_dict());
        continue;

      default:
        return 0;
    }
  }

  return ERROR__INTERNAL;
}

static inline int consume_expr() {
  char *start = cursor->p;
  _check(consume_optional_expr());

  if (start == cursor->p) {
    return ERROR__UNEXPECTED;
  }
  return 0;
}

// consume destructuring declaration
// TODO(samthor): this is basically a dictionary
static int consume_definition_destructuring(int special) {
#ifdef DEBUG
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
          cursor->special = SPECIAL__BASE | SPECIAL__PROPERTY | SPECIAL__DECLARE | special;
        }
        cursor_next();
        break;
      }

      case TOKEN_ARRAY:
        if (start == TOKEN_BRACE) {
          // this is a computed property name
          _check(consume_expr());
          break;
        }
        _check(consume_definition_destructuring(special));
        break;

      case TOKEN_BRACE:
        // nb. doesn't make sense in object context, but harmless
        _check(consume_definition_destructuring(special));
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
          _check(consume_definition_destructuring(special));
          break;

        case TOKEN_SYMBOL:  // reentry
        case TOKEN_LIT:
          cursor->type = TOKEN_SYMBOL;
          cursor->special = SPECIAL__BASE | SPECIAL__DECLARE | special;
          cursor_next();
          break;
      }
    }

    // consume default
    if (cursor->special == MISC_EQUALS) {
      cursor_next();
      _check(consume_expr());
    }
  }
}

// consumes a single definition (e.g. `catch (x)` or x in `function(x, y) {}`
static int consume_definition(int special) {
  int is_spread = 0;

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
      cursor_next();
      break;

    case TOKEN_BRACE:
    case TOKEN_ARRAY:
      _check(consume_definition_destructuring(special));
      break;

    default:
      if (is_spread) {
        debugf("destructuring had bad spread");
        return ERROR__UNEXPECTED;
      }
      return 0;  // can't consume this
  }

  switch (cursor->special) {
    case LIT_IN:
    case LIT_OF:
      cursor->type = TOKEN_OP;
      // fall-through

    case MISC_EQUALS:
      cursor_next();
      _check(consume_expr());
      break;
  }

  return 0;
}

// consumes a number of comma-separated definitions
static int consume_definition_list(int special) {
  for (;;) {
    _check(consume_definition(special));
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

  _check(consume_definition_list(0));
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
    _check(consume_expr());
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
      return consume_definition(0);

    case LIT_FOR:
      break;

    default:
      return consume_expr();
  }

  if (cursor->type == TOKEN_SEMICOLON) {
    // fine, ignore left block
  } else if (cursor->special & _MASK_DECL) {
    // started with "var" etc
    int special = cursor->special == LIT_VAR ? SPECIAL__TOP : 0;
    cursor->type = TOKEN_KEYWORD;
    cursor_next();

    _check(consume_definition_list(special));
  } else {
    // TODO: this can consume "x in y", which means we can get "x in y of z", disallowed
    _check(consume_expr());

    // "x of y" or "x in y"
    if (cursor->special == LIT_OF || cursor->special == LIT_IN) {
      cursor->type = TOKEN_KEYWORD;
      cursor_next();
      return consume_expr();
    }
  }

  // after left block, check for semicolon
  if (cursor->type != TOKEN_SEMICOLON) {
    debugf("expected 1st semicolon");
    return ERROR__UNEXPECTED;
  }
  cursor_next();

  // consume middle block (skip if semicolon)
  if (cursor->type != TOKEN_SEMICOLON) {
    _check(consume_expr());
  }
  if (cursor->type != TOKEN_SEMICOLON) {
    debugf("expected 2nd semicolon");
    return ERROR__UNEXPECTED;
  }
  cursor_next();

  // consume right block (skip if close)
  if (cursor->type == TOKEN_CLOSE) {
    return 0;
  }
  return consume_expr();
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
      return consume_expr();
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

      _check(consume_expr());

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
        _check(consume_expr());
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
  } else if (!cursor->special) {
    return consume_expr();
  }

  if (cursor->special & _MASK_CONTROL) {
    return consume_control();
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

  return consume_expr();
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