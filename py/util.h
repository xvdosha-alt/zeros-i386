#ifndef MP_UTIL_H
#define MP_UTIL_H

#include "config.h"

size_t mp_strlen(const char *s);
int mp_strcmp(const char *a, const char *b);
void mp_memcpy(void *dst, const void *src, size_t n);
void mp_memset(void *dst, int v, size_t n);
char *mp_strncpy(char *dst, const char *src, size_t n);

#endif
