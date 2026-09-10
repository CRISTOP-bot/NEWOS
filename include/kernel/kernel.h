#ifndef KERNEL_KERNEL_H
#define KERNEL_KERNEL_H

#include <kernel/types.h>

#define ARRAY_SIZE(a)       (sizeof(a) / sizeof((a)[0]))
#define ALIGN_UP(v, a)      (((uintptr_t)(v) + (uintptr_t)(a) - 1) & \
                             ~((uintptr_t)(a) - 1))
#define ALIGN_DOWN(v, a)    (((uintptr_t)(v)) & ~((uintptr_t)(a) - 1))
#define IS_ALIGNED(v, a)    ((((uintptr_t)(v)) & ((uintptr_t)(a) - 1)) == 0)
#define MIN(a, b)           ((a) < (b) ? (a) : (b))
#define MAX(a, b)           ((a) > (b) ? (a) : (b))
#define CLAMP(v, lo, hi)    (MIN(MAX((v), (lo)), (hi)))
#define STATIC_ASSERT(c)    _Static_assert(c, #c)

#define UNUSED(x)           ((void)(x))

#define KiB(n)              ((n) * 1024ull)
#define MiB(n)              ((n) * 1024ull * 1024ull)
#define GiB(n)              ((n) * 1024ull * 1024ull * 1024ull)

#define PAGE_SIZE           4096ull
#define PAGE_SHIFT          12ull

#define COMPILER_BARRIER()  __asm__ volatile("" ::: "memory")

#define likely(x)           __builtin_expect(!!(x), 1)
#define unlikely(x)         __builtin_expect(!!(x), 0)

#define KERNEL_BASE_VA      0xffffffff80000000ull
#define PHYS_LOAD_BASE      0x100000ull   /* physical load address */
#define HI_HALF_START       0xffff800000000000ull

/* Kernel virtual memory layout.
 *
 * The x86_64 kernel is linked at KERNEL_BASE_VA (PML4 index 511) above its
 * physical load (PHYS_LOAD_BASE). The whole of low physical memory is also
 * covered by a direct physical map at DIRECT_PHYS_BASE (PML4 index 508)
 * using 1 GiB pages; phys_to_virt()/virt_to_phys() translate through it.
 *
 * The low 47-bit half (< 2^47) belongs to user address spaces; the kernel's
 * half (PML4 indices 256..511) is shared by value in every address space.
 */
#define KERNEL_IMAGE_SIZE   MiB(6)          /* high-half window for the image */
#define DIRECT_PHYS_BASE    0xfffffe0000000000ull
#define DIRECT_MAP_SIZE     GiB(4)          /* phys 0 .. 4 GiB direct-mapped */

#define USER_SPACE_BASE     0x400000ull     /* first user loadable VA        */
#define USER_SPACE_END      0x0000800000000000ull /* end of the low half     */
#define USER_STACK_SIZE     MiB(1)
#define USER_STACK_TOP      0x00007fffffffe000ull

static inline uintptr_t phys_to_virt(uintptr_t phys)
{
    return phys + DIRECT_PHYS_BASE;
}

static inline uintptr_t virt_to_phys(uintptr_t virt)
{
    /* Kernel image window (linked at KERNEL_BASE_VA, loaded at PHYS_LOAD_BASE). */
    if (virt >= KERNEL_BASE_VA && virt < KERNEL_BASE_VA + KERNEL_IMAGE_SIZE)
        return (virt - KERNEL_BASE_VA) + PHYS_LOAD_BASE;
    /* Direct physical map. */
    if (virt >= DIRECT_PHYS_BASE && virt < DIRECT_PHYS_BASE + DIRECT_MAP_SIZE)
        return virt - DIRECT_PHYS_BASE;
    /* Early identity window (boot structures while the identity map is up). */
    if (virt < GiB(1))
        return virt;
    return ~(uintptr_t)0;
}

#endif