#include "util.h"

size_t mp_strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int mp_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

void mp_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--)
        *d++ = *s++;
}

void mp_memset(void *dst, int v, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    while (n--)
        *d++ = (uint8_t)v;
}

char *mp_strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i + 1 < n && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
    return dst;
}
