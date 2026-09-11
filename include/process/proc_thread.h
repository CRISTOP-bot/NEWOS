#ifndef KERNEL_THREAD_H
#define KERNEL_THREAD_H

#include <core/core_types.h>

struct process;

/* A thread owns a kernel stack and a saved entry context. In this phase
 * every process has exactly one thread; the scheduler extension adds
 * general save/restore of register state. */

#define THREAD_KERNEL_STACK_SIZE (16 * 1024)
#define THREAD_KERNEL_STACK_ORDER 2      /* frames: 2^order */

struct thread {
    struct process *process;
    u64 kernel_stack_top;   /* kernel-half VA (direct map) */
    u64 entry_sp;           /* points at the iretq frame */
    u64 cr3;                /* physical PML4 of the address space */
    u64 sp;                 /* scratch for the exit context switch */
};

/* Assembly primitives (arch/x86_64/thread/thread.S). */
void x64_context_switch(u64 *from, u64 to);
void x64_context_save(u64 *from);
void x64_enter_user(struct thread *t) __attribute__((noreturn));
void x64_goto_stack(u64 new_rsp, void (*fn)(u64), u64 arg) __attribute__((noreturn));

/* Allocate a kernel stack and prepare the iretq frame on it. cr3 is the
 * physical PML4 of the thread's address space. Returns 0 on success. */
int thread_setup_user(struct thread *t, uintptr_t entry, uintptr_t user_rsp,
                      u64 cr3);

/* Kernel-side resume context, shared with process_exit(). */
u64  thread_init_stack(void);
void thread_capture_resume(void);
void thread_resume_to(u64 *from) __attribute__((noreturn));

#endif