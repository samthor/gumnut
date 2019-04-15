#include <string.h>  // just for types

void bzero(void *s, size_t n) {
  unsigned char *p = s;
  while (n--) {
    *p++ = (unsigned char) 0;
  }
}

void *memset(void *s, int c, size_t n) {
  unsigned char *p = s;
  while (n--) {
    *p++ = (unsigned char) c;
  }
  return s;
}

void *memcpy(void *__restrict dest, const void *__restrict src, size_t n) {
  char *dp = dest;
  const char *sp = src;
  while (n--) {
    *dp++ = *sp++;
  }
  return dest;
}

int isalpha(int c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int isdigit(int c) {
  return (c >= '0' && c <= '9');
}

int isalnum(int c) {
  return isalpha(c) || (c >= '0' && c <= '9');
}

int isspace(int c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

char *strchr(const char *s, int c) {
  char *p = (char *) s;
  while (*p) {
    if (c == *p) {
      return p;
    }
    ++p;
  }
  return NULL;
}

size_t strlen(const char *s) {
  const char *p = s;
  while (*s) {
    ++s;
  }
  return s - p;
}

char *strstr(const char *s1, const char *s2) {
  size_t n = strlen(s2);
  while (*s1) {
    if (!memcmp(s1++, s2, n)) {
      return ((char *) s1) - 1;
    }
  }
  return 0;
}

int memcmp(const void *s1, const void *s2, size_t n) {
  const unsigned char *p1 = s1, *p2 = s2;
  while (n--) {
    if (*p1 != *p2) {
      return *p1 - *p2;
    }
    p1++;
    p2++;
  }
  return 0;
}

void *memchr(const void *s, int c, size_t n) {
  unsigned char *p = (unsigned char *) s;
  while (n--) {
    if (*p == (unsigned char) c) {
      return p;
    }
    ++p;
  }
  return 0;
}
