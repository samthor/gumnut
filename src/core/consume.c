
#include "token.h"
#include "debug.h"

#define input_end (td->end)


// consume regexp "/foobar/"
static inline int blepi_consume_slash_regexp(char *p) {
#ifdef DEBUG
  if (p[0] != '/') {
    debugf("failed to consume slash_regexp, no slash");
    return 0;
  }
#endif
  char *start = p;
  int is_charexpr = 0;

  while (++p < input_end) {
    switch (*p) {
      case '/':
        // nb. already known not to be a comment `//`
        if (is_charexpr) {
          continue;
        }

        // eat trailing flags
        do {
          ++p;
        } while (isalnum(*p));
        return p - start;

      case '\n':
        return p - start;

      case '[':
        is_charexpr = 1;
        continue;

      case ']':
        is_charexpr = 0;
        continue;

      case '\\':
        if (p[1] == '/' || p[1] == '[' || p[1] == '\\') {
          ++p;  // we can only escape these
        }
        continue;
    }
  }

  return p - start;
}

static inline int blepi_maybe_consume_alnum_group(char *p) {
  if (p[0] != '{') {
    return 0;
  }

  int len = 1;
  for (;;) {
    char c = p[len];
    ++len;

    if (c == '}') {
      return len;
    } else if (!isalnum(c)) {
      return ERROR__UNEXPECTED;
    }
  }
}

static inline int blepi_consume_basic_string(char *p, int *line_no) {
#ifdef DEBUG
  if (p[0] != '\'' && p[0] != '"') {
    debugf("got bad string starter");
    return 0;
  }
#endif
  char *start = p;

  for (;;) {
    ++p;
    switch (*p) {
      case '\0':
        if (input_end == p) {
          return p - start;
        }
        continue;

      case '\n':
        // nb. not valid here
        ++(*line_no);
        continue;

      case '\\':
        if (p[1] == *start || p[1] == '\\') {
          ++p;  // the only things we care about escaping
        }
        continue;

      case '"':
      case '\'':
        if (*p == *start) {
          ++p;
          return p - start;
        }
        // do nothing, we found the other one
    }
  }
}

static inline int blepi_consume_template(char *p, int *line_no) {
  // p[0] will be ` or }
#ifdef DEBUG
  if (p[0] != '`' && p[0] != '}') {
    debugf("consume_template got bad starter");
    return 0;
  }
#endif
  char *start = p;

  for (;;) {
    ++p;
    switch (*p) {
      case '\0':
        if (input_end == p) {
          return p - start;
        }
        continue;

      case '\n':
        ++(*line_no);
        continue;

      case '\\':
        if (p[1] == '$' || p[1] == '`' || p[1] == '\\') {
          ++p;  // we can only escape these
        }
        continue;

      case '$':
        if (p[1] == '{') {
          return 2 + p - start;
        }
        continue;

      case '`':
        return 1 + p - start;
    }
  }
}

// consumes spaces/comments between tokens
static inline int blepi_consume_void(char *p, int *line_no) {
  int line_no_delta = 0;
  char *start = p;

  for (;;) {
    switch (*p) {
      case ' ':    // 32
      case '\t':   //  9
      case '\v':   // 11
      case '\f':   // 12
      case '\r':   // 13
        ++p;
        continue;

      case '\n':   // 10
        ++p;
        ++line_no_delta;
        continue;

      case '/': {  // 47
        char next = p[1];
        if (next == '/') {
          p = memchr(p, '\n', input_end - p);
          if (p == 0) {
            p = input_end;
          }
          continue;
        } else if (next != '*') {
          break;
        }

        // consuming multiline
        // nb. this can't use memchr because it's looking for both * and \n
        p += 2;
        do {
          char c = *p;
          if (c == '*') {
            if (p[1] == '/') {
              p += 2;
              break;
            }
          } else if (c == '\n') {
            ++line_no_delta;
          }
        } while (++p < input_end);
        continue;
      }
    }

    break;  // unhandled, break below
  }

  (*line_no) += line_no_delta;
  return p - start;
}

// consumes number, assumes first char is valid (dot or digit)
static inline int blepi_consume_number(char *p) {
#ifdef DEBUG
  if (!(isdigit(p[0]) || (p[0] == '.' && isdigit(p[1])))) {
    debugf("consume_number got bad digit");
    return 0;
  }
#endif
  int len = 1;
  char c = p[1];
  for (;;) {
    if (!(isalnum(c) || c == '.' || c == '_')) {  // letters, dots, etc- misuse is invalid, so eat anyway
      break;
    }
    c = p[++len];
  }
  return len;
}