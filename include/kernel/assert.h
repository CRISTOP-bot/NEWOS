#ifndef KERNEL_ASSERT_H
#define KERNEL_ASSERT_H

#include <kernel/types.h>
#include <kernel/kernel.h>
#include <kernel/panic.h>

#if defined(CONFIG_DEBUG) && CONFIG_DEBUG

#define assert(expr) \
    do { \
        if (unlikely(!(expr))) { \
            panic("ASSERTION FAILED: %s at %s:%d", \
                  #expr, __FILE__, __LINE__); \
        } \
    } while (0)

#define BUG_ON(cond) \
    do { \
        if (unlikely(cond)) { \
            panic("BUG_ON triggered: %s at %s:%d", \
                  #cond, __FILE__, __LINE__); \
        } \
    } while (0)

#else

#define assert(expr)        ((void)0)
#define BUG_ON(cond)        ((void)0)

#endif

#define BUG() \
    panic("BUG at %s:%d", __FILE__, __LINE__)

#endif