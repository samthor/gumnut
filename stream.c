
/**
stack_inc:
  bzero(curr, sizeof(tokenstack));
  curr->type = type;
  curr->reok = 1;
  curr->initial = (type == TOKEN_BRACE);
*/

int stream_next(tokendef *d, token *out) {
  int slash_is_op = !d->stack[d->depth].reok;
  int ret = raw_token(d, slash_is_op, out);
  if (ret || out->type == TOKEN_COMMENT) {
    return ret;
  }

  token *p = &d->prev;
  tokenstack *curr = d->stack + d->depth;
  int initial = curr->initial;
  curr->initial = 0;

  switch (out->type) {
    case TOKEN_SEMICOLON:
      curr->reok = 1;
      curr->initial = (d->depth == 0 || curr->type == TOKEN_BRACE);
      if (curr->pending_colon || curr->pending_function || curr->pending_hoist_brace) {
        out->invalid = 1;
        curr->pending_colon = 0;
        curr->pending_function = 0;
        curr->pending_hoist_brace = 0;
      }
      break;

    case TOKEN_COMMA:
    case TOKEN_SPREAD:
    case TOKEN_ARROW:
    case TOKEN_T_BRACE:
      curr->reok = 1;
      break;

    case TOKEN_TERNARY:
      if (curr->pending_colon == __MAX_PENDING_COLON - 1) {
        return ERROR__INTERNAL;
      }
      ++curr->pending_colon;
      curr->reok = 1;
      break;

    case TOKEN_COLON:
      curr->reok = 1;
      if (curr->pending_colon) {
        // nb. following is value (and non-initial)
        --curr->pending_colon;
      } else if (p->type == TOKEN_LIT) {
        curr->initial = 1;  // label, now initial again
      } else {
        out->invalid = 1;  // colon unexpected
      }
      break;

    case TOKEN_DOT:
    case TOKEN_STRING:
    case TOKEN_REGEXP:
    case TOKEN_NUMBER:
      curr->reok = 0;
      break;

    case TOKEN_OP:
      if (is_double_addsub(out->p, out->len)) {
        break;  // does nothing to value state
      }

      if (out->len == 1 && out->p[0] == '*') {
        if (p->type == TOKEN_LIT && p->len == 8 && !memcmp(p->p, "function", 8)) {
          // TODO: following {} is a generator
        }
      }

      curr->reok = 1;
      break;

    case TOKEN_ARRAY:
      curr->reok = (out->p[0] == '[');
      break;

    case TOKEN_BRACE: {
      if (out->p[0] == '}') {
        curr->initial = initial;  // reset in case this is the end of a statement
        break;  // set below
      }

      tokenstack *up = curr - 1;
      int is_block = up->pending_function || up->initial || p->type == TOKEN_ARROW ||
          (p->type == TOKEN_LIT && is_block_creator(p->p, p->len));

      if (up->pending_hoist_brace) {
        if (p->type == TOKEN_LIT && !memcmp(p->p, "ex", 2)) {
          // corner case: allow `class Foo extends {} {}`, invalid but would break tokenizer
          is_block = 0;
        } else {
          // if we were pending a hoist brace (i.e., top-level function/class), then allow a RE
          // after the final }, as we don't have implicit value
          up->reok = 1;
          up->pending_hoist_brace = 0;
          up->initial = initial;
        }
      } else if (p->type == TOKEN_ARROW || up->pending_function) {
        up->reok = 0;  // block types that return a value (hoist function is above branch)
      } else {
        up->reok = is_block;
      }

      printf("got brace, is_block=%d reok=%d\n", is_block, up->reok);
      break;
    }

    case TOKEN_PAREN: {
      if (out->p[0] == ')') {
        curr->initial = initial;  // reset in case this is the end of a control
        break;  // set below
      }

      tokenstack *up = curr - 1;
      if (p->type != TOKEN_LIT) {
        up->reok = 0;
        break;
      }

      int was_async = is_async(p->p, p->len);
      if (was_async) {
        // TODO: following => is definitely asyncable
        // (but we might not get a =>)
      }

      // if the previous statement was a literal and has control parens (e.g., if, for) then the
      // result of ) doesn't have implicit value
      if (!(up == d->stack || up->type == TOKEN_BRACE)) {
        break;  // not brace above us
      }
      // FIXME: we don't need to calcluate if we are in a dict, but it should be harmless if we
      // do (op or regexp is invalid here anyway)

      up->reok = !was_async && is_control_paren(p->p, p->len);
      up->initial = up->reok;
      break;
    }

    case TOKEN_LIT:
      if (is_hoist_keyword(out->p, out->len)) {
        // look for prior async on function
        if (out->p[0] == 'f') {
          curr->pending_function = 1;
          if (p->type == TOKEN_LIT && is_async(p->p, p->len)) {
            // TODO: following {} is definitely asyncable
          }
        }
        // if this is an initial function or class, then the end is not a value
        int phb = initial;
        if (!phb && (curr == d->stack || curr->type == TOKEN_BRACE)) {
          // not if it's preceded by an oplike or an op
          phb = !((p->type == TOKEN_LIT && is_oplike(p->p, p->len)) || p->type == TOKEN_OP);
        }
        curr->pending_hoist_brace = phb;
      } else if (is_allows_re(out->p, out->len)) {
        if (out->p[0] == 'a') {
          // TODO: "await" is only the case if inside an asyncable
        } else if (out->p[0] == 'y') {
          // TODO: "yield" is only the case if inside a generator
        }
        curr->reok = 1;
        break;
      }
      curr->reok = 0;
      break;
  }

  d->prev = *out;
  return 0;
}