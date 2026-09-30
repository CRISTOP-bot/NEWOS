#ifndef NEWOS_LIBC_ERRNO_H
#define NEWOS_LIBC_ERRNO_H

/* Minimal errno: single global (no threads yet), common codes. */

extern int errno;

#define EPERM 1
#define ENOENT 2
#define EIO 5
#define EBADF 9
#define ENOMEM 12
#define EACCES 13
#define EEXIST 17
#define ENOTTY 25
#define EINVAL 22
#define ENOSPC 28
#define EROFS 30

const char *strerror(int e);

#endif
