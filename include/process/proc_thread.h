#ifndef KERNEL_THREAD_H
#define KERNEL_THREAD_H

#include <core/core_types.h>
#include <x86_fpu.h>

struct process;
struct x64_iframe;

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
    /* Last RIP parked by thread_save_entry, to detect slot corruption
     * between park and handoff (resume would jump to garbage). */
    u64 last_park_rip;
    /* Persistent sleep deadline: blocking SLEEP re-traps from scratch on
     * every resume, so the deadline must survive across invocations or
     * each resume would restart the full interval and sleep forever
     * while a peer stays runnable. `sleeping` marks it valid. */
    u64 sleep_due;
    int sleeping;
    /* x86_64 FS base (TLS). Applied by the interrupt epilogue on every
     * return to ring 3; changed by SYS_ARCH_PRCTL. 0 until the program
     * installs its TLS block. */
    u64 fsbase;
    /* x87/SSE state: the FXSAVE image the interrupt epilogue swaps on every
     * scheduler handoff (see x86_fpu.h). LAST and 16-byte aligned, because
     * FXSAVE/FXRSTOR fault with #GP on a misaligned image; the containing
     * struct process is 16-byte aligned by kzalloc. */
    u8 fpu[X64_FPU_AREA_SIZE] __attribute__((aligned(X64_FPU_AREA_ALIGN)));
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

/* SysV entry convention for spawned programs: rdi = argc, rsi = argv. */
void thread_set_user_args(struct thread *t, u64 argc, u64 argv);

/* Park a live tick frame into the thread's reserved entry slot (see
 * thread_setup_user): the scheduler calls this instead of pointing
 * entry_sp at the live stack, which later syscalls would overwrite. */
void thread_save_entry(struct thread *t, struct x64_iframe *f);

/* Kernel-side resume context, shared with process_exit(). */
u64  thread_init_stack(void);
void thread_capture_resume(void);
void thread_resume_to(u64 *from) __attribute__((noreturn));

#endif