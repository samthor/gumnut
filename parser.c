
#include "parser.h"
#include "lit.h"
#include "token.h"
#include <string.h>


#ifdef DEBUG
#include <stdio.h>
#define debugf(...) fprintf(stderr, __VA_ARGS__)
#else
#define debugf (void)sizeof
#endif

static int consume_statement();


static int parser_skip = 0;

#define cursor (&(td->curr))


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
    if (cursor->type == TOKEN_SEMICOLON && cursor->hash == SPECIAL__SAMELINE) { \
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

static inline int consume_dict() {
  return ERROR__INTERNAL;
}

static int consume_expr() {
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
        return ERROR__INTERNAL;

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

// consume destructuring declaration
static int consume_definition_destructuring(int special) {
#ifdef DEBUG
  if (cursor->type != TOKEN_BRACE && cursor->type != TOKEN_ARRAY) {
    debugf("destructuring did not start with { or [\n");
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
          cursor->hash = SPECIAL__BASE | SPECIAL__PROPERTY;
        } else {
          // e.g. "const {x} = ...", x is a symbol, decl and property
          cursor->type = TOKEN_SYMBOL;
          cursor->hash = SPECIAL__BASE | SPECIAL__PROPERTY | SPECIAL__DECLARE | special;
        }
        cursor_next();
        break;
      }

      case TOKEN_ARRAY:
        if (start == TOKEN_BRACE) {
          // this is a computed property name
          _check(consume_expr());  // TODO: could consume group
          break;
        }
        _check(consume_definition_destructuring(special));
        break;

      case TOKEN_BRACE:
        // nb. doesn't make sense in object context, but harmless
        _check(consume_definition_destructuring(special));
        break;

      case TOKEN_OP:
        if (cursor->hash == MISC_COMMA) {
          // nb. solo comma
          cursor_next();
          continue;
        }
        if (cursor->hash == MISC_SPREAD) {
          // this basically effects the next lit or destructured thing
          cursor_next();
          continue;
        }
        // fall-through

      default:
        debugf("got unexpected inside object destructuring\n");
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
          cursor->hash = SPECIAL__BASE | SPECIAL__DECLARE | special;
          cursor_next();
          break;
      }
    }

    // consume default
    if (cursor->hash == MISC_EQUALS) {
      cursor_next();
      _check(consume_expr());
    }
  }
}

static int consume_definition(int special) {
  int is_spread = 0;

  if (cursor->hash == MISC_SPREAD) {
    cursor_next();
    is_spread = 1;
  }

  switch (cursor->type) {
    case TOKEN_SYMBOL:  // reentry
    case TOKEN_LIT:
      // nb. might be unsupported (e.g. "this" or "import"), but invalid in this case
      cursor->type = TOKEN_SYMBOL;
      cursor->hash = SPECIAL__BASE | SPECIAL__DECLARE | special;
      cursor_next();
      break;

    case TOKEN_BRACE:
    case TOKEN_ARRAY:
      // TODO: destructuring
      debugf("TODO: destructuring\n");
      return ERROR__INTERNAL;
      // _check(consume_destructuring(context, special));
      // break;

    default:
      if (is_spread) {
        debugf("destructuring had bad spread\n");
        return ERROR__UNEXPECTED;
      }
      return 0;  // can't consume this
  }

  switch (cursor->hash) {
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

static int consume_definition_list(int special) {
  for (;;) {
    _check(consume_definition(special));
    if (cursor->hash != MISC_COMMA) {
      return 0;
    }
    cursor_next();
  }
}

static int consume_arg_group() {
  if (cursor->type != TOKEN_PAREN) {
    debugf("arg_group didn't start with paren\n");
    return ERROR__UNEXPECTED;
  }
  cursor_next();

  _check(consume_definition_list(SPECIAL__TOP));
  if (cursor->type != TOKEN_CLOSE) {
    debugf("arg_group did not finish with close\n");
    return ERROR__UNEXPECTED;
  }
  cursor_next();
  return 0;
}

static int consume_function(int special) {
  debugf("consuming func\n");
  int is_async = 0;
  int is_generator = 0;

  if (cursor->hash == LIT_ASYNC) {
    is_async = 1;
    cursor->type = TOKEN_KEYWORD;
    debugf("cursor was on async\n");
    cursor_next();
  }

  if (cursor->hash != LIT_FUNCTION) {
    debugf("function did not start with 'function'\n");
    return ERROR__UNEXPECTED;
  }
  cursor->type = TOKEN_KEYWORD;
  cursor_next();

  if (cursor->hash == MISC_STAR) {
    is_generator = 1;
    cursor_next();
  }

  if (cursor->type == TOKEN_LIT) {
    // TODO: write special
    debugf("got name: %.*s\n", cursor->len, cursor->p);
    cursor_next();
  } else if (special) {
    // TODO: emit empty
  }
  debugf("function, generator=%d async=%d\n", is_generator, is_async);

  _STACK_BEGIN(STACK__FUNCTION);

  _check(consume_arg_group());
  _check(consume_statement());

  _STACK_END();
  return 0;
}

static int consume_class(int special) {
  return ERROR__UNEXPECTED;
}

static int consume_control() {
#ifdef DEBUG
  if (!(cursor->hash & _MASK_CONTROL)) {
    debugf("expected _MASK_CONTROL for consume_control");
    return ERROR__INTERNAL;
  }
#endif

  int control_hash = cursor->hash;

  _STACK_BEGIN(STACK__CONTROL);
  cursor->type = TOKEN_KEYWORD;
  cursor_next();

  // match "for" and "for await"
  if (control_hash == LIT_FOR) {
    if (cursor->hash == LIT_AWAIT) {
      cursor->type = TOKEN_KEYWORD;
      cursor_next();
    }
  }

  if (cursor->type == TOKEN_PAREN) {
    // special-case catch, which creates a local scoped var
    if (control_hash == LIT_CATCH) {
      cursor_next();

      return ERROR__INTERNAL;  // TODO
//      _check(consume_single_definition(context, SPECIAL__DECLARE));

      if (cursor->type != TOKEN_CLOSE) {
        debugf("could not find closer of catch()\n");
        return ERROR__UNEXPECTED;
      }
      cursor_next();
    } else {
      return ERROR__INTERNAL;  // TODO
//      _check(consume_expr_group(context));
    }
  }

  _check(consume_statement());

  // special-case trailing "while(...)" for a 'do-while'
  if (control_hash == LIT_DO) {

    return ERROR__INTERNAL;

  }

  _STACK_END();
  return 0;
}

static int consume_statement() {
  debugf("consume_statement: %d (%.*s %d)\n", cursor->type, cursor->len, cursor->p, cursor->hash);

  switch (cursor->type) {
    case TOKEN_EOF:
    case TOKEN_COLON:
      return ERROR__UNEXPECTED;

    case TOKEN_CLOSE:
      return 0;

    case TOKEN_BRACE:
      _STACK_BEGIN(STACK__BLOCK);
      cursor_next();

      do {
        printf("consuming block statement\n");
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

  switch (cursor->hash) {
    case LIT_DEFAULT:
      _STACK_BEGIN(STACK__LABEL);

      cursor->type = TOKEN_KEYWORD;
      cursor_next();

      if (cursor->type != TOKEN_COLON) {
        debugf("no : after default\n");
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
        debugf("no : after case\n");
        return ERROR__UNEXPECTED;
      }
      cursor_next();

      _STACK_END();
      break;

    case LIT_ASYNC:
      blep_token_peek();
      if (td->peek.hash != LIT_FUNCTION) {
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
      return ERROR__UNEXPECTED;
  }

  if (!(cursor->hash & _MASK_MASQUERADE)) {
    if (blep_token_peek() == TOKEN_COLON) {
      // nb. "await:" is invalid in async functions, but it's nonsensical anyway
      // we restart this function to parse as label
      cursor->type = TOKEN_LABEL;
      return consume_statement();
    }
  } else if (!cursor->hash) {
    return consume_expr();
  }

  if (cursor->hash & _MASK_CONTROL) {
    return consume_control();
  }

  // catches things like "enum", "protected", which are keywords but largely unhandled
  if (cursor->hash & (_MASK_KEYWORD | _MASK_STRICT_KEYWORD)) {
    debugf("got fallback TOKEN_KEYWORD\n");
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
  debugf("zero cursor: type=%d\n", cursor->type);
}

int blep_parser_run() {
  return consume_statement();
}