#include "../include/stdlib.h"
#include "../include/string.h"
#include "../../abi/syscall_abi.h"
#include "syscall.h"

long libc_write(int fd, const void *buf, unsigned long len)
{
    return libc_sys3(SYS_WRITE, fd, (long)buf, (long)len);
}

void exit(int code)
{
    extern atexit_fn atexit_table[];
    extern int atexit_count;
    while (atexit_count > 0)
        atexit_table[--atexit_count]();
    libc_sys1(SYS_EXIT, code);
    for (;;)
        ;
}

#define ATEXIT_MAX 8
atexit_fn atexit_table[ATEXIT_MAX];
int atexit_count;

int atexit(atexit_fn fn)
{
    if (!fn || atexit_count >= ATEXIT_MAX)
        return -1;
    atexit_table[atexit_count++] = fn;
    return 0;
}

int abs(int v)
{
    return v < 0 ? -v : v;
}

long labs(long v)
{
    return v < 0 ? -v : v;
}

static unsigned long rand_next = 1;

void srand(unsigned seed)
{
    rand_next = seed ? seed : 1;
}

int rand(void)
{
    /* xorshift32: no division, no FPU, decent spread for games/demos. */
    unsigned long x = rand_next;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rand_next = x;
    return (int)((x >> 8) & RAND_MAX);
}
