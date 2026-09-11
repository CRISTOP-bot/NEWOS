#include <process/proc_thread.h>
#include <mm/mm_pmm.h>
#include <core/core.h>
#include <mm/mm_vmm.h>
#include <core/core_printk.h>
#include <iru_string.h>

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

/* Build the iretq frame used to enter user mode. The frame lives at the top
 * of a freshly allocated kernel stack (direct-map VA). */
int thread_setup_user(struct thread *t, uintptr_t entry, uintptr_t user_rsp,
                      u64 cr3)
{
    u64 frame;
    if (pmm_block_alloc(THREAD_KERNEL_STACK_ORDER, &frame) != 0)
        return -1;

    u8 *base = (u8 *)phys_to_virt(frame << PAGE_SHIFT);
    memset(base, 0, THREAD_KERNEL_STACK_SIZE);
    uintptr_t top = (uintptr_t)base + THREAD_KERNEL_STACK_SIZE;

    /* Frames are pushed down: ss, rsp, rflags, cs, rip. */
    u64 *fr = (u64 *)top;
    *--fr = 0x23;               /* ss:  user data, RPL 3 */
    *--fr = user_rsp;
    *--fr = 0x202;              /* rflags: IF set */
    *--fr = 0x1b;               /* cs:  user code, RPL 3 */
    *--fr = entry;              /* rip */

    t->kernel_stack_top = top;
    t->entry_sp = (uintptr_t)fr;
    t->cr3 = cr3;
    return 0;
}