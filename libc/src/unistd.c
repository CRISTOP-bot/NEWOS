#include "../include/unistd.h"
#include "../include/fcntl.h"
#include "../../abi/syscall_abi.h"
#include "syscall.h"
#include "../include/errno.h"

long read(int fd, void *buf, unsigned long len)
{
    return libc_sys3(SYS_READ, fd, (long)buf, (long)len);
}

long write(int fd, const void *buf, unsigned long len)
{
    return libc_sys3(SYS_WRITE, fd, (long)buf, (long)len);
}

int close(int fd)
{
    return (int)libc_sys1(SYS_CLOSE, fd);
}

long lseek(int fd, long off, int whence)
{
    return libc_sys3(SYS_LSEEK, fd, off, whence);
}

void *sbrk(long increment)
{
    static unsigned long current;
    unsigned long old, next;
    long result;

    if (!current) {
        result = libc_sys1(SYS_BRK, 0);
        if (result <= 0) {
            errno = ENOMEM;
            return (void *)-1;
        }
        current = (unsigned long)result;
    }
    old = current;
    if (increment >= 0) {
        if ((unsigned long)increment > (~0ul) - old) {
            errno = ENOMEM;
            return (void *)-1;
        }
        next = old + (unsigned long)increment;
    } else {
        unsigned long shrink = (unsigned long)(-(increment + 1)) + 1;
        if (shrink > old) {
            errno = ENOMEM;
            return (void *)-1;
        }
        next = old - shrink;
    }
    result = libc_sys1(SYS_BRK, (long)next);
    if ((unsigned long)result != next) {
        errno = ENOMEM;
        return (void *)-1;
    }
    current = next;
    return (void *)old;
}

int open(const char *path, int flags, ...)
{
    /* The kernel speaks Linux O_* flags directly (it translates them to
     * the VFS model, honours O_TRUNC/O_APPEND). */
    long fd = libc_sys2(SYS_OPEN, (long)path, flags);
    return fd < 0 ? -1 : (int)fd;
}
