#include <kernel/pmm.h>
#include <kernel/printk.h>
#include <kernel/kernel.h>
#include <mm/pmm/allocator.h>
#include <libk/string.h>

/* PMM front end: selects a backend implementation. Uses the bitmap
 * allocator (bitmap.c); the buddy backend (buddy.c) is layered on top
 * when CONFIG_PMM_BUDDY is enabled. */

void pmm_allocator_init(u64 mem_start, u64 mem_end)
{
    pmm_init(mem_start, mem_end);

    /* Reserve all frames below the end of the kernel image. */
    extern u64 _kernel_end;
    u64 kernel_end_phys = virt_to_phys((uintptr_t)&_kernel_end);
    kernel_end_phys = ALIGN_UP(kernel_end_phys, PAGE_SIZE);

    if (kernel_end_phys > mem_start && kernel_end_phys < mem_end)
        pmm_reserve_region(mem_start, kernel_end_phys);
    else
        pr_warn("PMM: kernel image outside tracked range, skipping reserve\n");
}