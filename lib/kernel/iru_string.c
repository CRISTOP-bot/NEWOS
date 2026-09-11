#include <iru_string.h>

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

size_t strnlen(const char *s, size_t maxlen)
{
    size_t n = 0;
    while (n < maxlen && s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && (*a == *b)) {
        a++;
        b++;
        n--;
    }
    if (!n)
        return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++))
        ;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    char *d = dst;
    while (n && *src) {
        *d++ = *src++;
        n--;
    }
    while (n) {
        *d++ = '\0';
        n--;
    }
    return dst;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c)
            return (char *)s;
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c)
            last = s;
        s++;
    }
    if (c == '\0')
        return (char *)s;
    return (char *)last;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d)
        d++;
    while ((*d++ = *src++))
        ;
    return dst;
}

char *strstr(const char *haystack, const char *needle)
{
    size_t n = strlen(needle);
    if (!n)
        return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle && !memcmp(haystack, needle, n))
            return (char *)haystack;
    }
    return NULL;
}

char *strtok(char *str, const char *delim)
{
    static char *next = NULL;
    char *tok, *p;

    if (str)
        next = str;

    if (!next)
        return NULL;

    while (*next && strchr(delim, *next))
        next++;

    if (!*next) {
        next = NULL;
        return NULL;
    }

    tok = next;
    p = tok;
    while (*p && !strchr(delim, *p))
        p++;

    if (*p) {
        *p++ = '\0';
        next = p;
    } else {
        next = NULL;
    }

    return tok;
}