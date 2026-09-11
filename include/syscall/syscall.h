#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <x86_frame.h>

/* System call dispatcher. Runs on the user interrupt frame and returns the
 * value that ends up in RAX for the guest (SYS_EXIT never returns). */
long syscall_dispatch(struct x64_iframe *f);

#endif