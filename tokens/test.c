
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TOKEN_MAX   12
#define TOKEN_COUNT 100

static int seen[TOKEN_COUNT];
static int seen_at = 0;

static const char always_keyword[] =
    " async break case catch class const continue debugger default delete do else enum export"
    " extends finally for function if import new return static switch throw try typeof var void"
    " while with ";

static const char always_strict_keyword[] =
    " implements package protected interface private public ";

static const char label_ish[] = 
    " false in instanceof null super this true ";

static const char optional_keyword[] =
    " as await from let yield ";

int hash(char *s, int len) {
  char sec = 0;
  if (len > 1) {
    sec = s[1];
  }
  return s[0] + (sec << 8) + ((len & 0xff) << 16);
}

int announce(char *name, char *s, int len) {
  if (len == 0) {
    len = strlen(s);
  }

  char buf[TOKEN_MAX];

  memset(buf, ' ', TOKEN_MAX);
  if (name) {
    int nlen = strlen(name);
    if (nlen >= TOKEN_MAX) {
      nlen = TOKEN_MAX - 1;
    }
    strncpy(buf, name, nlen);
  } else {
    strncpy(buf, s, len);
    for (int i = 0; i < TOKEN_MAX; ++i) {
      buf[i] = toupper(buf[i]);
    }
  }
  buf[TOKEN_MAX - 1] = 0;

  int h = hash(s, len);

  // zzzz checking seen
  for (int i = 0; i < seen_at; ++i) {
    if (seen[i] == h) {
      printf("// err: dup %s, %d\n", buf, h);
      return 1;
    }
  }

  printf("#define LIT_%s %d\n", buf, h);

  seen[seen_at++] = h;
  if (seen_at == TOKEN_COUNT) {
    printf("// err: too many tokens\n");
    exit(1);
  }
  return 0;
}

int process(const char *raw) {
  char *at = (char *) raw;

  for (;;) {
    char *s = at + 1;
    char *next = strchr(s, ' ');
    if (!next) {
      break;
    }

    int len = next - s;
    if (len > TOKEN_MAX - 1) {
      len = TOKEN_MAX - 1;
    }

    if (announce(NULL, s, len)) {
      return 1;
    }

    at = next;
  }

  return 0;
}

int main() {
  bzero(seen, sizeof(seen));

  process(always_keyword);
  process(always_strict_keyword);
  process(label_ish);
  process(optional_keyword);

  // we only need to match some ops
  announce("_STAR", "*", 0);
  announce("_INC", "++", 0);
  announce("_DEC", "--", 0);

  return 0;
}
