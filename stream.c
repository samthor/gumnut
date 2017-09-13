/*
 * Copyright 2017 Sam Thorogood. All rights reserved.
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

#include <string.h>
#include "stream.h"
#include "utils.h"

streamdef prsr_stream_init() {
  streamdef sd;
  bzero(&sd, sizeof(sd));
  streamstack *first = sd.stack;
  first->reok = 1;
  first->is_brace = 1;
  first->initial = 1;
  return sd;
}

static int dexc_inc(streamdef *sd) {
  if (sd->dexc == __EXC_STACK_SIZE - 1) {
    return ERROR__INTERNAL;
  }
  ++sd->dexc;
  streamexc *e = sd->exc + sd->dexc;
  bzero(e, sizeof(streamexc));
  return 0;
}

static int dexc_pop_arrow(streamdef *sd) {
  int start = sd->dexc;
  while (sd->exc[sd->dexc].arrow && sd->dexc) {
    --sd->dexc;
  }
  int count = start - sd->dexc;
  if (count) {
    printf("popped %d arrows\n", start - sd->dexc);
  }
  return count;
}

static int stack_mod(streamdef *sd, token *t) {
  if (t->invalid) {
    return 0;  // tokenizer keeps track of whether {}'s are correctly aligned
  }

  int dec = 0;
  switch (t->type) {
    case TOKEN_BRACE:
      dec = t->p[0] == '}';
      // fall-through
    case TOKEN_T_BRACE:
      break;

    case TOKEN_ARRAY:
      dec = t->p[0] == ']';
      break;

    case TOKEN_PAREN:
      dec = t->p[0] == ')';
      break;

    default:
      return 0;
  }

  if (dec) {
    if (!sd->depth) {
      return ERROR__INTERNAL;
    }

    // pop arrow states, remove if func
    if (sd->stack[sd->depth].exc) {
      dexc_pop_arrow(sd);
      streamstack *curr = sd->stack + sd->depth;
      if (curr->exc) {
        if (!sd->dexc) {
          return ERROR__INTERNAL;
        }
        --sd->dexc;
      }
    }

    --sd->depth;
    return 1;
  }
  if (sd->depth == __STACK_SIZE - 1) {
    return ERROR__INTERNAL;
  }
  ++sd->depth;
  streamstack *curr = sd->stack + sd->depth;
  bzero(curr, sizeof(streamstack));
  curr->reok = 1;
  if (t->type == TOKEN_BRACE) {
    curr->is_brace = 1;
    curr->initial = 1;
  }
  return 1;
}

static int stream_next(streamdef *sd, token *out) {
  token *p = &sd->prev;
  streamstack *curr = sd->stack + sd->depth;

  // async () without =>
  if (curr->pending_async &&
      !curr->is_dict &&
      !curr->pending_function &&
      out->type != TOKEN_ARROW) {
    curr->pending_async = 0;  // we has "async ()" _without_ a following =>
  }

  int ret = stack_mod(sd, out);
  if (ret < 0) {
    return ret;
  } else if (ret) {
    streamstack *update = sd->stack + sd->depth;  // we changed
    if (update < curr && curr->exc) {
      if (!sd->dexc) {
        return ERROR__INTERNAL;  // should never go <0
      }
      --sd->dexc;
    }
    curr = update;
  }
  int initial = curr->initial;
  curr->initial = 0;

  switch (out->type) {
    case TOKEN_EOF:
      dexc_pop_arrow(sd);
      break;

    case TOKEN_SEMICOLON:
      curr->reok = 1;
      curr->initial = curr->is_brace;
      if (curr->pending_colon || curr->pending_function || curr->pending_hoist_brace) {
        out->invalid = 1;
        curr->pending_colon = 0;
        curr->pending_function = 0;
        curr->pending_hoist_brace = 0;
      }
      dexc_pop_arrow(sd);
      break;

    case TOKEN_ARROW: {
      int ret = dexc_inc(sd);
      if (ret) {
        return ret;
      }
      streamexc *e = sd->exc + sd->dexc;
      e->arrow = 1;
      e->async = curr->pending_async;
      curr->pending_async = 0;
      curr->reok = 1;
      break;
    }

    case TOKEN_COMMA:
      if (curr->is_dict) {
        curr->dict_left = 1;
        curr->dict_left_async = 0;
        break;
      }
      // fall-through
    case TOKEN_SPREAD:
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
      if (curr->is_dict) {
        // nb. invalid in class-style dicts, but shouldn't apper anyway
        curr->dict_left = 0;  // now allow anything
        curr->dict_left_async = 0;
      } else if (curr->pending_colon) {
        --curr->pending_colon;  // nb. following is value (and non-initial)
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
        if (curr->dict_left ||
            (p->type == TOKEN_LIT && p->len == 8 && !memcmp(p->p, "function", 8))) {
          curr->pending_generator = 1;
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

      streamstack *up = curr - 1;

      if (!up->reok && p->line_no != out->line_no) {
        // FIXME: this needs to happen in a bunch of places (not just brace), ugh
        dexc_pop_arrow(sd);  // pop if we find unexpected brace
      }

      curr->exc = up->is_dict && up->dict_left;  // brace on dict left is a function

      int is_block = up->pending_function || up->initial || up->dict_left ||
          p->type == TOKEN_ARROW || (p->type == TOKEN_LIT && is_block_creator(p->p, p->len));

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
          curr->exc = up->pending_function;
        }
      } else if (p->type == TOKEN_ARROW) {
        streamexc *e = sd->exc + sd->dexc;
        e->arrow = 0;
        up->reok = 0;  // end of () => {...} is a value
        curr->exc = 1;
      } else if (up->pending_function) {
        up->reok = 0;  // we are a block, but also generate a value
        curr->exc = 1;
      } else {
        up->reok = is_block;
      }

      curr->is_dict = !is_block;
      curr->dict_left = curr->is_dict;
      printf("got brace, is_dict=%d reok=%d generator=%d async=%d exc=%d\n", curr->is_dict, up->reok, up->pending_generator, up->pending_async, curr->exc);

      if (curr->exc && p->type != TOKEN_ARROW) {
        int ret = dexc_inc(sd);
        if (ret) {
          return ret;
        }
        streamexc *e = sd->exc + sd->dexc;
        e->async = up->pending_async;
        e->generator = up->pending_generator;
      }

      // reset function state, we found it
      up->pending_function = 0;
      up->pending_generator = 0;
      up->pending_async = 0;
      break;
    }

    case TOKEN_PAREN: {
      if (out->p[0] == ')') {
        curr->initial = initial;  // reset in case this is the end of a control
        break;  // set below
      }

      streamstack *up = curr - 1;
      if (p->type != TOKEN_LIT) {
        up->reok = 0;
        break;
      }

      int was_async = is_async(p->p, p->len);
      if (was_async) {
        if (!up->is_dict) {
          if (p->line_no != out->line_no) {
            // TODO: move line_no check up? if "async\n()" maybe give up early
            was_async = 0;
            goto paren_done;  // "async ()" can't have separating newline
          }
        } else if (up->dict_left_async <= 1) {
          break;  // zero or one async in dict, so it was the name
        }
        // nb. {} or => is now asyncable (although we might not get =>)
        up->pending_async = 1;
      } else if (up->dict_left_async) {
        printf("got open paren with left_async=%d\n", up->dict_left_async);
        up->pending_async = 1;
      }

      if (!up->is_brace || up->is_dict) {
        break;  // not within a block
      }

      // if the previous statement was a literal and has control parens (e.g., if, for) then the
      // result of ) doesn't have implicit value
paren_done:
      up->reok = !was_async && is_control_paren(p->p, p->len);
      up->initial = up->reok;
      break;
    }

    case TOKEN_LIT:
      if (p->line_no != out->line_no) {
        if ((p->type == TOKEN_LIT && is_oplike(p->p, p->len)) || p->type == TOKEN_OP) {
          // FIXME: needs to happen way more places
          dexc_pop_arrow(sd);
        }
      }

      if (curr->dict_left) {
        if (is_async(out->p, out->len)) {
          if (curr->dict_left_async) {
            curr->dict_left_async = 2;  // count, allows for "{async async() {}}"
          } else {
            curr->dict_left_async = 1;
          }
        }
        break;
      } else if (is_async(out->p, out->len)) {
        // hold onto it in case we're a top-level async function
        curr->initial = initial;
      } else if (is_hoist_keyword(out->p, out->len)) {
        // look for prior async on function
        if (out->p[0] == 'f') {
          curr->pending_function = 1;
          if (p->type == TOKEN_LIT && p->line_no == out->line_no && is_async(p->p, p->len)) {
            // nb. "async function" can't have separating newline
            curr->pending_async = 1;
          }
        }
        // if this is an initial function or class, then the end is not a value
        int phb = initial;
        if (!phb && curr->is_brace) {
          // not if it's preceded by an oplike, op, or async
          phb = !((p->type == TOKEN_LIT && is_oplike(p->p, p->len)) ||
              p->type == TOKEN_OP || curr->pending_async);
        }
        curr->pending_hoist_brace = phb;
      } else if (is_allows_re(out->p, out->len)) {
        streamexc *e = sd->exc + sd->dexc;
        if (out->p[0] == 'a' && !e->async) {
          // await is only the case if inside an asyncable
        } else if (out->p[0] == 'y' && !e->generator) {
          // yield is only the case if inside a generator
          // nb. if we see \n after this, these aren't merged (doesn't matter for stream)
        } else {
          curr->reok = 1;
          break;
        }
      } else if (p->type == TOKEN_LIT && is_async(p->p, p->len)) {
        // this is "async x"
        curr->pending_async = 1;
      } else {
        // clear for sanity
        curr->pending_async = 0;
      }
      curr->reok = 0;
      break;
  }

  return 0;
}

int prsr_stream_next(streamdef *sd, token *out) {
  if (out->type == TOKEN_COMMENT) {
    return 0;  // don't process comment
  }

  int ret = stream_next(sd, out);
  if (ret) {
    return ret;
  }
  sd->prev = *out;
  sd->slash_is_op = !sd->stack[sd->depth].reok;
  return 0;
}