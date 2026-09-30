#ifndef LIBK_LOCK_H
#define LIBK_LOCK_H

#include <core/core_types.h>
#include <iru_string.h>

#define SPINLOCK_UNLOCKED 0
#define SPINLOCK_LOCKED   1

struct spinlock {
    volatile u32 locked;
    const char *name;
};

static inline void spinlock_init(struct spinlock *lock, const char *name)
{
    lock->locked = SPINLOCK_UNLOCKED;
    lock->name   = name;
}

static inline void spinlock_lock(struct spinlock *lock)
{
    u32 expected = SPINLOCK_UNLOCKED;
    __asm__ volatile(
        "1: lock cmpxchgl %2, %0\n"
        "   jnz 2f\n"
        "   jmp 3f\n"
        "2: pause\n"
        "   cmpl %1, %0\n"
        "   je 1b\n"
        "   jmp 2b\n"
        "3:\n"
        : "+m"(lock->locked), "+a"(expected)
        : "r"(SPINLOCK_LOCKED)
        : "memory", "cc");
}

static inline void spinlock_unlock(struct spinlock *lock)
{
    /* A plain store is a valid release on x86 (TSO); the locked RMW in
     * spinlock_lock() + this compiler barrier order all prior accesses.
     * `lock` prefixed MOV would be a #UD (LOCK requires an RMW op). */
    __asm__ volatile("movl $0, %0\n" : "=m"(lock->locked) :: "memory");
}

static inline int spinlock_trylock(struct spinlock *lock)
{
    u32 expected = SPINLOCK_UNLOCKED;
    __asm__ volatile(
        "lock cmpxchgl %2, %0\n"
        : "+m"(lock->locked), "+a"(expected)
        : "r"(SPINLOCK_LOCKED)
        : "memory", "cc");
    return expected == SPINLOCK_UNLOCKED;
}

static inline int spinlock_is_locked(struct spinlock *lock)
{
    return lock->locked != SPINLOCK_UNLOCKED;
}

static inline u64 irq_save(void)
{
    u64 flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void irq_restore(u64 flags)
{
    __asm__ volatile("push %0; popfq" : : "r"(flags) : "memory");
}

static inline u64 spinlock_acquire_irqsave(struct spinlock *lock)
{
    u64 flags = irq_save();
    spinlock_lock(lock);
    return flags;
}

static inline void spinlock_release_irqrestore(struct spinlock *lock, u64 flags)
{
    spinlock_unlock(lock);
    irq_restore(flags);
}

#endif