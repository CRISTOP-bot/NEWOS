#include <kernel/thread.h>
#include <kernel/pmm.h>
#include <kernel/kernel.h>
#include <kernel/vmm.h>
#include <kernel/printk.h>
#include <libk/string.h>

/* Per-thread kernel stacks and the kernel-side resume context that a user
 * process returns to when it exits (via SYS_EXIT or a fault). */

static u64 s_init_stack_top;
static u64 s_resume_sp;

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
 * (a self-context-switch saves the running frame and returns immediately). */
void thread_capture_resume(void)
{
    x64_context_save(&s_resume_sp);
}

uintptr_t thread_resume_sp_value(void)
{
    return (uintptr_t)s_resume_sp;
}

void thread_resume_to(u64 *from)
{
    x64_context_switch(from, s_resume_sp);
    __builtin_unreachable();
}

uintptr_t thread_resume_word(int idx)
{
    if (idx < 0 || idx > 7)
        return 0;
    return *(u64 *)(s_resume_sp + (u64)idx * 8);
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
    printk("thread: user-stack frame=%llx base=%p top=%p\n",
           frame, base, (void *)top);

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