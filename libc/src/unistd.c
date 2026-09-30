#include "../include/unistd.h"
#include "../include/fcntl.h"
#include "../../abi/syscall_abi.h"
#include "syscall.h"

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

int open(const char *path, int flags, ...)
{
    /* The kernel speaks Linux O_* flags directly (it translates them to
     * the VFS model, honours O_TRUNC/O_APPEND). */
    long fd = libc_sys2(SYS_OPEN, (long)path, flags);
    return fd < 0 ? -1 : (int)fd;
}
