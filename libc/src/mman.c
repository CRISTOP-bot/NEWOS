#include "../include/sys/mman.h"
#include "../include/errno.h"
#include "../../abi/syscall_abi.h"
#include "syscall.h"

void *mmap(void *addr, size_t length, int prot, int flags, int fd,
           off_t offset)
{
    register long r10 __asm__("r10") = flags;
    register long r8 __asm__("r8") = fd;
    register long r9 __asm__("r9") = offset;
    long result;

    __asm__ __volatile__("int $0x80"
                         : "=a"(result)
                         : "a"(SYS_MMAP), "D"((long)addr),
                           "S"((long)length), "d"(prot),
                           "r"(r10), "r"(r8), "r"(r9)
                         : "memory");
    if (result < 0) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return (void *)result;
}

int munmap(void *addr, size_t length)
{
    long result = libc_sys2(SYS_MUNMAP, (long)addr, (long)length);
    if (result < 0) {
        errno = EINVAL;
        return -1;
    }
    return (int)result;
}

int mprotect(void *addr, size_t length, int prot)
{
    long result = libc_sys3(SYS_MPROTECT, (long)addr, (long)length, prot);
    if (result < 0) {
        errno = EINVAL;
        return -1;
    }
    return (int)result;
}
