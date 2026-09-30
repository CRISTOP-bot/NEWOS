#include "../include/errno.h"

int errno;

const char *strerror(int e)
{
    switch (e) {
    case EPERM:
        return "operation not permitted";
    case ENOENT:
        return "no such file or directory";
    case EIO:
        return "I/O error";
    case EBADF:
        return "bad file descriptor";
    case ENOMEM:
        return "out of memory";
    case EACCES:
        return "permission denied";
    case EEXIST:
        return "file exists";
    case ENOTTY:
        return "not a typewriter";
    case EINVAL:
        return "invalid argument";
    case ENOSPC:
        return "no space left on device";
    case EROFS:
        return "read-only file system";
    default:
        return "unknown error";
    }
}
