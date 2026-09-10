#include <kernel/multiboot2.h>
#include <kernel/bootinfo.h>
#include <kernel/printk.h>
#include <kernel/pmm.h>
#include <kernel/kernel.h>
#include <mm/pmm/allocator.h>
#include <libk/string.h>

/* Parses the multiboot2 info structure handed over by the boot loader and
 * feeds the memory map into the PMM. */

#define TAG_NEXT(tag) \
    ((struct multiboot2_tag *)((u8 *)(tag) + \
        (((tag)->size + 7) & ~7)))

void mbi_parse_mmap(u64 mbi_phys)
{
    /* The multiboot info lives in low physical memory while the early
     * identity map (VA == phys below 1 GiB) is active. */
    struct multiboot2_info *info =
        (struct multiboot2_info *)(uintptr_t)mbi_phys;

    if (!info || info->total_size == 0) {
        pr_warn("mbi: no multiboot info (magic check failed earlier)\n");
        return;
    }

    u64 mem_start = 0, mem_end = 0;
    int got_region = 0;

    struct multiboot2_tag *tag =
        (struct multiboot2_tag *)((u8 *)info + 8);

    for (; tag->type != MULTIBOOT2_TAG_END;
         tag = TAG_NEXT(tag)) {
        switch (tag->type) {
        case MULTIBOOT2_TAG_BASIC_MEMINFO: {
            u32 *m = (u32 *)(tag + 1);
            pr_info("mbi: base mem %u KiB, extended %u KiB\n",
                    m[0], m[1]);
            break;
        }
        case MULTIBOOT2_TAG_CMDLINE: {
            const char *cmd = (const char *)(tag + 1);
            pr_info("mbi: cmdline '%s'\n", cmd);
            boot_capture_cmdline(cmd);
            break;
        }
        case MULTIBOOT2_TAG_BOOTLOADER: {
            const char *name = (const char *)(tag + 1);
            pr_info("mbi: bootloader '%s'\n", name);
            break;
        }
        case MULTIBOOT2_TAG_MMAP: {
            struct multiboot2_mmap *mmap_tag = (void *)(tag + 1);
            struct multiboot2_mmap_entry *entry = (void *)(mmap_tag + 1);

            while ((u8 *)entry < (u8 *)tag + tag->size) {
                if (entry->type == MULTIBOOT2_MMAP_RAM) {
                    u64 start = entry->base_addr;
                    u64 end = entry->base_addr + entry->length;

                    /* Skip low memory below the kernel image; span all
                     * available regions so fragmented multiboot maps
                     * still cover the whole of RAM. */
                    if (end > PHYS_LOAD_BASE) {
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
                entry = (struct multiboot2_mmap_entry *)
                    ((u8 *)entry + mmap_tag->entry_size);
            }
            break;
        }
        case MULTIBOOT2_TAG_FRAMEBUFFER: {
            pr_info("mbi: framebuffer present\n");
            break;
        }
        default:
            break;
        }
    }

    if (!got_region) {
        pr_warn("mbi: no usable RAM regions in memory map\n");
        return;
    }

    pr_info("mbi: usable RAM %llx - %llx (%llu KiB)\n",
            (u64)mem_start, (u64)mem_end,
            (u64)((mem_end - mem_start) >> 10));

    pmm_allocator_init(mem_start, mem_end);
}