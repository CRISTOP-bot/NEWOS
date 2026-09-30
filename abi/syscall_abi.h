/* Forwarding shim.
 *
 * The single source of truth for the user/kernel syscall ABI is
 * include/abi/syscall_abi.h. Kernel code reaches it as
 * <abi/syscall_abi.h> (via -Iinclude); userland (libc/, user/lib/) has
 * historically reached it by relative path, which lands here. Keeping a
 * second copy of the definitions is what let kernel and userland drift
 * apart, so this file only forwards.
 */

#ifndef ABI_SYSCALL_ABI_FORWARD_H
#define ABI_SYSCALL_ABI_FORWARD_H

#include "../include/abi/syscall_abi.h"

#endif
