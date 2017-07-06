
typedef struct {
  const char *name;
  const char *input;
  int *expected;  // zero-terminated token types
} testdef;

int run_testdef(testdef *td);

// defines a test for prsr: args must have a trailing comma
#define _test(_name, _input, ...) \
{ \
  testdef td; \
  td.name = _name; \
  td.input = _input; \
  int v[] = {__VA_ARGS__ TOKEN_EOF}; \
  td.expected = v; \
  ok |= run_testdef(&td); \
}
