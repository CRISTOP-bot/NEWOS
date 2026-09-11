#include <core/core_bootinfo.h>
#include <core/core_printk.h>
#include <mm/mm_pmm.h>
#include <core/core.h>
#include <mm/mm_pmm.h>
#include <iru_string.h>

/* Parses the QEMU PVH start-info structure and feeds the memory map into
 * the PMM. Used by the x86_64 boot path when QEMU loads the ELF kernel
 * directly (-kernel) via the "Xen" ELF note. */

void pvh_parse_mmap(u64 pvh_phys)
{
    /* The boot info and every table it points to live in low physical
     * memory while the early identity map (VA == phys below 1 GiB) is
     * active, so boot structures are reached through identity pointers. */
    struct hvm_start_info *si =
        (struct hvm_start_info *)(uintptr_t)pvh_phys;

    if (!si || si->magic != PVH_MAGIC) {
        pr_warn("pvh: start info magic mismatch (got 0x%x)\n",
                si ? si->magic : 0u);
        return;
    }

    u64 mem_start = 0, mem_end = 0;
    int got_region = 0;

    pr_info("pvh: start_info flags=%x version=%u entries=%u\n",
            si->flags, si->version, si->memmap_entries);

    /* RAM is the contiguous run that begins at or below the kernel image
     * load address, extended while the next region abuts it. A plain
     * min-start/max-end over every E820_RAM entry would bridge the MMIO
     * holes (VGA window, PCI/APIC space) and hand out non-RAM frames. The
     * end is also clamped to the direct physical map so every allocatable
     * frame has a kernel mapping. */
    if (si->memmap_entries) {
        struct hvm_memmap_table_entry *base =
            (struct hvm_memmap_table_entry *)(uintptr_t)si->memmap_paddr;

        for (u32 i = 0; i < si->memmap_entries; i++) {
            if (base[i].type != E820_RAM)
                continue;
            u64 start = base[i].addr;
            u64 end = start + base[i].size;
            if (end <= PHYS_LOAD_BASE)
                continue;
            if (start < PHYS_LOAD_BASE)
                start = PHYS_LOAD_BASE;
            if (!got_region || start < mem_start) {
                mem_start = start;
                mem_end = end;
                got_region = 1;
            } else if (start == mem_start && end > mem_end) {
                mem_end = end;
            }
        }

        int progress;
        do {
            progress = 0;
            for (u32 i = 0; i < si->memmap_entries; i++) {
                if (base[i].type != E820_RAM)
                    continue;
                u64 start = base[i].addr;
                u64 end = start + base[i].size;
                if (end <= PHYS_LOAD_BASE)
                    continue;
                if (start < PHYS_LOAD_BASE)
                    start = PHYS_LOAD_BASE;
                if (start <= mem_end && end > mem_end) {
                    mem_end = end;
                    progress = 1;
                }
            }
        } while (progress);

        if (mem_end > DIRECT_MAP_SIZE)
            mem_end = DIRECT_MAP_SIZE;
    }

    /* Same QEMU quirk as the memmap: SIF_CMDLINE is never set in flags
     * even though cmdline_paddr is valid, so key off the pointer. */
    if (si->cmdline_paddr) {
        const char *cmd = (const char *)(uintptr_t)si->cmdline_paddr;
        pr_info("pvh: cmdline '%s'\n", cmd);
        boot_capture_cmdline(cmd);
    }

    if (!got_region) {
        pr_warn("pvh: no usable RAM regions in memory map\n");
        return;
    }

    pr_info("pvh: usable RAM %llx - %llx (%llu KiB)\n",
            (u64)mem_start, (u64)mem_end,
            (u64)((mem_end - mem_start) >> 10));

    pmm_allocator_init(mem_start, mem_end);
}