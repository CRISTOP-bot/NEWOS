#include <mm/mm_pmm.h>
#include <core/core_printk.h>
#include <core/core.h>
#include <mm/mm_pmm.h>
#include <iru_string.h>

/* PMM front end: selects a backend implementation. Uses the bitmap
 * allocator (bitmap.c); the buddy backend (buddy.c) is layered on top
 * when CONFIG_PMM_BUDDY is enabled. */

void pmm_allocator_init(u64 mem_start, u64 mem_end)
{
    struct pmm_ram_range run = { mem_start, mem_end };
    pmm_allocator_init_ranges(&run, 1);
}

int pmm_allocator_init_ranges(const struct pmm_ram_range *runs, int count)
{
    u64 mem_start = ~0ull, mem_end = 0;
    int used = 0;

    for (int i = 0; i < count; i++) {
        if (runs[i].end <= runs[i].start)
            continue;
        if (runs[i].start < mem_start)
            mem_start = runs[i].start;
        if (runs[i].end > mem_end)
            mem_end = runs[i].end;
        used++;
    }
    if (!used)
        return -1;

    pmm_init(mem_start, mem_end);

    /* Mask the whole window, then release exactly the RAM runs. Every
     * frame outside a usable run (MMIO holes, VGA window, PCI/APIC space)
     * stays reserved and is never handed out. */
    pmm_reserve_region(mem_start, mem_end);
    for (int i = 0; i < count; i++) {
        if (runs[i].end > runs[i].start)
            pmm_release_region(runs[i].start, runs[i].end);
    }

    /* Reserve exactly the kernel image. The native boot path loads the
     * image at PHYS_LOAD_BASE (== g_kernel_phys_base here), while the
     * Limine path loads it at a bootloader-chosen high physical address,
     * so the reservation must start at the runtime base, not mem_start. */
    extern u64 _kernel_end;
    u64 kernel_base_phys = ALIGN_DOWN(g_kernel_phys_base, PAGE_SIZE);
    u64 kernel_end_phys = virt_to_phys((uintptr_t)&_kernel_end);
    kernel_end_phys = ALIGN_UP(kernel_end_phys, PAGE_SIZE);

    if (kernel_end_phys > kernel_base_phys && kernel_end_phys <= mem_end)
        pmm_reserve_region(kernel_base_phys, kernel_end_phys);
    else
        pr_warn("PMM: kernel image outside tracked range, skipping reserve\n");

    return 0;
}