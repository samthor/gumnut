#include <string.h>

int isnum(char c) {
  return c >= '0' && c <= '9';
}

// nb. buf must contain words start/end with space, aka " test foo "
int in_space_string(const char *big, char *s, int len) {
  // TODO: do something better? strstr is probably fast D:
  // search for: space + candidate + space
  char cand[16];
  memcpy(cand+1, s, len);
  cand[0] = ' ';
  cand[len+1] = ' ';
  cand[len+2] = 0;

  return strstr(big, cand) != NULL;
}

int is_keyword(char *s, int len) {
  if (len > 10 || len < 2) {
    return 0;  // no statements <2 ('if' etc) or >10 ('implements')
  }
  for (int i = 0; i < len; ++i) {
    if (s[i] < 'a' || s[i] > 'z') {
      return 0;  // only a-z
    }
  }
  // nb. does not contain 'in' or 'instanceof', as they are ops
  static const char v[] =
    " await break case catch class const continue debugger default delete do else enum export"
    " extends finally for function if implements import interface let new package private"
    " protected public return static super switch throw try typeof var void while with yield ";
  return in_space_string(v, s, len);
}

int is_control_keyword(char *s, int len) {
  if (len > 5 || len < 2) {
    return 0;  // no control <2 ('if' etc) or >5 ('while' etc)
  }
  static const char v[] = " if for switch while with ";
  return in_space_string(v, s, len);
}

int is_asi_keyword(char *s, int len) {
  if (len > 9 || len < 5) {
    return 0;  // no asi <5 ('yield' etc) or >9 ('continue')
  }
  static const char v[] = " break continue return throw yield ";
  return in_space_string(v, s, len);
}

int is_hoist_keyword(char *s, int len) {
  return (len == 5 && !memcmp(s, "class", 5)) || (len == 8 && !memcmp(s, "function", 8));
}
