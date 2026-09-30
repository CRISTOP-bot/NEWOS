#ifndef NEWOS_LIBC_STDLIB_H
#define NEWOS_LIBC_STDLIB_H

/* NEWOS libc: numbers, pseudo-random, heap (libc/src/stdlib.c,
 * libc/src/malloc.c). Integer only. */

#include "stddef.h"

int abs(int v);
long labs(long v);
int atoi(const char *s);
long atol(const char *s);

void srand(unsigned seed);
int rand(void);
#define RAND_MAX 32767

void *malloc(size_t n);
void free(void *p);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);

typedef void (*atexit_fn)(void);
int atexit(atexit_fn fn);

void exit(int code);

#endif
