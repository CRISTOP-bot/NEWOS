#include <process/proc_thread.h>
#include <process/proc_process.h>
#include <x86_cpu.h>
#include <mm/mm_pmm.h>
#include <core/core.h>
#include <mm/mm_vmm.h>
#include <core/core_printk.h>
#include <iru_string.h>
#include <x86_frame.h>

/* Per-thread kernel stacks and the kernel-side resume context that a user
 * process returns to when it exits (via SYS_EXIT or a fault). */

/* A captured resume frame holds the six callee-saved registers spilled by
 * x64_context_save() plus the return address: r15 r14 r13 r12 rbx rbp ret. */
#define RESUME_FRAME_WORDS 7

static u64 s_init_stack_top;
static u64 s_resume_sp;
static u64 s_resume_frame[RESUME_FRAME_WORDS];

/* Kernel-half init stack (direct map). Run_userland() lives on it; it is
 * also the context a process returns to after exiting. */
u64 thread_init_stack(void)
{
    if (!s_init_stack_top) {
        u64 frame;
        if (pmm_block_alloc(THREAD_KERNEL_STACK_ORDER, &frame) != 0) {
            printk("thread: failed to allocate init stack\n");
            return 0;
        }
        u8 *base = (u8 *)phys_to_virt(frame << PAGE_SHIFT);
        memset(base, 0, THREAD_KERNEL_STACK_SIZE);
        s_init_stack_top = (uintptr_t)base + THREAD_KERNEL_STACK_SIZE;
        printk("thread: init stack @ %p\n", (void *)s_init_stack_top);
    }
    return s_init_stack_top;
}

/* Capture the current RSP as the kernel resume point for a process exit
 * (a self-context-switch saves the running frame and returns immediately).
 *
 * The captured frame is copied into static storage right away: the capture
 * happens on the live init stack, and continuing to execute on that stack
 * (the first-boot path makes several more calls before dropping to ring 3)
 * would otherwise reuse and overwrite the saved region. */
void thread_capture_resume(void)
{
    x64_context_save(&s_resume_sp);
    u64 *frame = (u64 *)s_resume_sp;
    for (int i = 0; i < RESUME_FRAME_WORDS; i++)
        s_resume_frame[i] = frame[i];
}

void thread_resume_to(u64 *from)
{
    /* The init stack is quiescent here (we run on the exiting process's own
     * kernel stack), so restoring the saved frame in place is safe. */
    u64 *frame = (u64 *)s_resume_sp;
    for (int i = 0; i < RESUME_FRAME_WORDS; i++)
        frame[i] = s_resume_frame[i];
    x64_context_switch(from, s_resume_sp);
    __builtin_unreachable();
}

/* Build the user-entry frame used both for the initial drop to ring 3 and
 * (via the scheduler) for every resumption: a struct x64_iframe in the
 * interrupt-stub layout, so a preempted thread can be re-entered by the
 * generic context switch.
 *
 * Live interrupt frames (syscall/tick/exception) always push at
 * kernel_stack_top, and the C call chains below them (syscall body plus
 * a nested tick plus printk) can reach ~1.2 KiB down, so the entry frame
 * is parked in a dedicated slot near the BOTTOM of the 16 KiB kernel
 * stack: anywhere near the top would let ordinary kernel execution
 * overwrite the resume point, and every switch-back would consume
 * garbage. thread_save_entry() re-parks the tick frame into the same
 * slot for the same reason. */
#define THREAD_ENTRY_SLOT_OFF 256

static struct x64_iframe *thread_entry_slot(struct thread *t)
{
    return (struct x64_iframe *)(void *)(t->kernel_stack_top -
                                         THREAD_KERNEL_STACK_SIZE +
                                         THREAD_ENTRY_SLOT_OFF);
}

/* Park a live tick frame into the thread's reserved slot so later syscalls
 * on this stack cannot overwrite the resume point. Runs in tick context
 * (interrupts already disabled by the interrupt gate). */
void thread_save_entry(struct thread *t, struct x64_iframe *f)
{
    /* A parked frame must carry a kernel or user CS and come from the PIT
     * tick or the syscall gate. Anything else means garbage is being
     * parked, which would poison the next switch-back. */
    if ((f->cs != 0x08 && f->cs != 0x1b) ||
        (f->vec != 32 && f->vec != 0x80)) {
        printk("diag: POISON tick frame for %s/%u: cs=%llx vec=%llu rip=%p rsp=%p cur=%s/%u\n",
               t->process ? t->process->name : "?",
               (unsigned)(t->process ? t->process->pid : 0),
               f->cs, f->vec, (void *)f->rip, (void *)f->rsp,
               process_current() ? process_current()->name : "(null)",
               (unsigned)(process_current() ? process_current()->pid : 0));
        x64_dump_iframe(f);
        for (;;)
            cpu_hlt();
    }
    struct x64_iframe *slot = thread_entry_slot(t);
    memcpy(slot, f, sizeof(*slot));
    t->last_park_rip = f->rip;
}

int thread_setup_user(struct thread *t, uintptr_t entry, uintptr_t user_rsp,
                      u64 cr3)
{
    /* FXSAVE/FXRSTOR fault with #GP on a misaligned image, and the fault
     * would land in the epilogue (unrecoverable). Every thread is embedded in
     * a kzalloc'd struct process, so this catches an alignment regression
     * instead of trusting it: refuse the thread. */
    if (((u64)(uintptr_t)&t->fpu[0] & (X64_FPU_AREA_ALIGN - 1)) != 0) {
        printk("thread: FXSAVE image at %p is not %u-byte aligned\n",
               (void *)&t->fpu[0], (unsigned)X64_FPU_AREA_ALIGN);
        return -1;
    }

    u64 frame;
    if (pmm_block_alloc(THREAD_KERNEL_STACK_ORDER, &frame) != 0)
        return -1;

    u8 *base = (u8 *)phys_to_virt(frame << PAGE_SHIFT);
    memset(base, 0, THREAD_KERNEL_STACK_SIZE);
    uintptr_t top = (uintptr_t)base + THREAD_KERNEL_STACK_SIZE;

    t->kernel_stack_top = top;
    struct x64_iframe *fr = thread_entry_slot(t);
    fr->rip = entry;
    fr->cs = 0x1b;          /* user code, RPL 3 */
    fr->rflags = 0x202;     /* IF set */
    fr->rsp = user_rsp;
    fr->ss = 0x23;          /* user data, RPL 3 */

    t->entry_sp = (uintptr_t)&fr->rip;   /* &rip == the iretq argument */
    t->cr3 = cr3;
    t->last_park_rip = entry;

    /* Every program starts from the architectural reset state (fninit +
     * default MXCSR): it must never inherit the x87/SSE registers of whoever
     * ran here before. */
    x64_fpu_area_init(&t->fpu[0]);
    return 0;
}

/* SysV entry convention for spawned programs: rdi = argc, rsi = argv.
 * Pokes the parked entry frame (kernel memory, always addressable). */
void thread_set_user_args(struct thread *t, u64 argc, u64 argv)
{
    struct x64_iframe *fr = thread_entry_slot(t);
    fr->rdi = argc;
    fr->rsi = argv;
}