#ifndef UAPI_SYSCALL_H
#define UAPI_SYSCALL_H

/* User/kernel shared syscall ABI.
 *
 * System calls are triggered with `int $0x80` (trap gate, DPL 3):
 *     rax = syscall number
 *     rdi, rsi, rdx, r10, r8, r9 = arguments (SysV order)
 *     rax = return value (>= 0 on success, negative errno-ish on error)
 *
 * The only registers preserved guarantees: all GPRs except rax are restored
 * (rcx and r11 are clobbered by the instruction, matching the SysV ABI).
 */

#define SYS_WRITE   1
#define SYS_EXIT    2
#define SYS_GETPID  3

/* Conventional file descriptors. */
#define STDOUT_FILENO 1

/* Negative return values are errors (simple -1 for now). */
#define SYSCALL_RET_ERROR (-1)

#endif