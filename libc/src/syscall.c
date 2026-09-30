#include "syscall.h"

long libc_sys0(long nr)
{
    long r;
    __asm__ __volatile__("int $0x80" : "=a"(r) : "a"(nr) : "memory");
    return r;
}

long libc_sys1(long nr, long a)
{
    long r;
    __asm__ __volatile__("int $0x80"
                         : "=a"(r)
                         : "a"(nr), "D"(a)
                         : "memory");
    return r;
}

long libc_sys2(long nr, long a, long b)
{
    long r;
    __asm__ __volatile__("int $0x80"
                         : "=a"(r)
                         : "a"(nr), "D"(a), "S"(b)
                         : "memory");
    return r;
}

long libc_sys3(long nr, long a, long b, long c)
{
    long r;
    __asm__ __volatile__("int $0x80"
                         : "=a"(r)
                         : "a"(nr), "D"(a), "S"(b), "d"(c)
                         : "memory");
    return r;
}
