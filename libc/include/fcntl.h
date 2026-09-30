#ifndef NEWOS_LIBC_FCNTL_H
#define NEWOS_LIBC_FCNTL_H

/* POSIX open() flags: Linux x86_64 values, passed straight to the kernel
 * (SYS_OPEN translates them to the internal VFS model). */

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 00000100
#define O_EXCL 00000200
#define O_NOCTTY 00000400
#define O_TRUNC 00001000
#define O_APPEND 00002000
#define O_NONBLOCK 00004000

int open(const char *path, int flags, ...);

#endif
