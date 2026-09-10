#include <kernel/bootinfo.h>
#include <kernel/printk.h>
#include <kernel/pmm.h>
#include <kernel/kernel.h>
#include <mm/pmm/allocator.h>
#include <libk/string.h>

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

    /* QEMU does not set SIF_MEM_MAP in flags even though the table is
     * present, so key off memmap_entries rather than the flag bit. */
    if (si->memmap_entries) {
        struct hvm_memmap_table_entry *entry =
            (struct hvm_memmap_table_entry *)(uintptr_t)si->memmap_paddr;

        for (u32 i = 0; i < si->memmap_entries; i++, entry++) {
            if (entry->type == E820_RAM) {
                u64 start = entry->addr;
                u64 end = entry->addr + entry->size;

                /* Skip low memory below the kernel image; span all
                 * available regions so fragmented maps cover all RAM. */
                if (end <= PHYS_LOAD_BASE)
                    continue;
                if (start < PHYS_LOAD_BASE)
                    start = PHYS_LOAD_BASE;

                if (!got_region) {
                    mem_start = start;
                    mem_end = end;
                } else {
                    if (start < mem_start)
                        mem_start = start;
                    if (end > mem_end)
                        mem_end = end;
                }
                got_region = 1;
            }
        }
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