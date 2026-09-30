#ifndef NEWOS_LIBC_STDDEF_H
#define NEWOS_LIBC_STDDEF_H

/* Shared base types for the NEWOS libc (freestanding, no host headers). */

typedef unsigned long size_t;
typedef long ssize_t;

#define NULL ((void *)0)
#define EOF (-1)

#endif
