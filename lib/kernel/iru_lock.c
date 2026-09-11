#include <iru_lock.h>

/* Interrupt-state tracking for IRQ-safe critical sections.
 * On x86_64 this wraps CLI/STI around spinlock acquisition. */

u64 lock_save_and_disable_irq(void)
{
    u64 flags = 0;
    __asm__ volatile(
        "pushfq\n"
        "popq %0\n"
        : "=r"(flags)
        :
        : "memory");
    __asm__ volatile("cli" ::: "memory");
    return flags;
}

void lock_restore_irq(u64 flags)
{
    (void)flags;
    __asm__ volatile("sti" ::: "memory");
}