#ifndef NEWOS_LIBC_STDIO_H
#define NEWOS_LIBC_STDIO_H

/* NEWOS libc: unbuffered console output on fd 1/2 (libc/src/stdio.c).
 * Tiny printf: %s %d(int) %u %x %X(int) %p %c %%. Integer only.
 *
 * FILE streams are thin fd wrappers (no user-space buffering): fread
 * and fwrite map straight onto read/write/lseek. */

#include "stddef.h"

typedef __builtin_va_list va_list;

typedef struct {
    int fd;
    int err;
    int eof;
} FILE;

int putchar(int c);
int puts(const char *s);
int printf(const char *fmt, ...);
int vsnprintf(char *dst, size_t n, const char *fmt, va_list ap);
int snprintf(char *dst, size_t n, const char *fmt, ...);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int fprintf(FILE *f, const char *fmt, ...);

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
size_t fread(void *p, size_t s, size_t n, FILE *f);
size_t fwrite(const void *p, size_t s, size_t n, FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
void rewind(FILE *f);
char *fgets(char *s, int n, FILE *f);
int fputc(int c, FILE *f);
int fputs(const char *s, FILE *f);
int getc(FILE *f);
int putc(int c, FILE *f);
int getchar(void);
int fflush(FILE *f);
int ferror(FILE *f);
int feof(FILE *f);
void clearerr(FILE *f);
void perror(const char *s);
ssize_t getline(char **lp, size_t *cap, FILE *f);

#endif

