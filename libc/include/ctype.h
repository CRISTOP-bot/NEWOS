#ifndef NEWOS_LIBC_CTYPE_H
#define NEWOS_LIBC_CTYPE_H

/* Plain-C ctype (ASCII only, no locale, no tables needed). */

static inline int isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int isalpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static inline int isalnum(int c) { return isalpha(c) || isdigit(c); }
static inline int isspace(int c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}
static inline int isupper(int c) { return c >= 'A' && c <= 'Z'; }
static inline int islower(int c) { return c >= 'a' && c <= 'z'; }
static inline int iscntrl(int c) { return c < 0x20 || c == 0x7F; }
static inline int isprint(int c) { return c >= 0x20 && c < 0x7F; }
static inline int isxdigit(int c)
{
    return isdigit(c) || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}
static inline int toupper(int c)
{
    return islower(c) ? c - ('a' - 'A') : c;
}
static inline int tolower(int c)
{
    return isupper(c) ? c + ('a' - 'A') : c;
}

#endif
