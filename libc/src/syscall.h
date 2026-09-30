#ifndef NEWOS_LIBC_SRC_SYSCALL_H
#define NEWOS_LIBC_SRC_SYSCALL_H

/* Internal raw syscall stubs shared by libc translation units.
 * Not installed; userland programs use the public headers instead. */

long libc_sys0(long nr);
long libc_sys1(long nr, long a);
long libc_sys2(long nr, long a, long b);
long libc_sys3(long nr, long a, long b, long c);

#endif
