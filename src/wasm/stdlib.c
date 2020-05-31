#include <string.h>  // just for types

int isalpha(int c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int isdigit(int c) {
  return (c >= '0' && c <= '9');
}

int isalnum(int c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

int isspace(int c) {
  return c == ' ' || (c >= '\t' && c <= '\r');  // \t, \n, \v, \f, \r
}
