#include <abi/multiboot2.h>
#include <core/core_bootinfo.h>
#include <core/core_printk.h>
#include <mm/mm_pmm.h>
#include <core/core.h>
#include <mm/mm_pmm.h>
#include <iru_string.h>

/* Parses the multiboot2 info structure handed over by the boot loader and
 * feeds the memory map into the PMM. */

#define TAG_NEXT(tag) \
    ((struct multiboot2_tag *)((u8 *)(tag) + \
        (((tag)->size + 7) & ~7)))

/* Declare fb_init_early for GRUB multiboot2 framebuffer support. */
extern void fb_init_early(u64 phys, u64 width, u64 height, u64 pitch, u32 bpp, u8 model, u8 rs, u8 rn, u8 gs, u8 gn, u8 bs, u8 bn);

/* Declare framebuffer tag struct from multiboot2 spec. */
struct mb_fb_tag {
    u32 framebuffer_type;
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u16 framebuffer_width;
    u16 framebuffer_height;
    u16 framebuffer_bpp;
    u16 red_field_position;
    u16 red_mask_size;
    u16 green_field_position;
    u16 green_mask_size;
    u16 blue_field_position;
    u16 blue_mask_size;
} __attribute__((packed));

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

    struct pmm_ram_range runs[32];
    int nruns = 0;

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

            /* Collect every usable RAM run, clamped to the direct physical
             * map so every allocatable frame has a kernel mapping. Non-RAM
             * gaps (VGA window, PCI/APIC space) stay holes in the run list
             * and are reserved by the PMM front end. */
            while ((u8 *)entry < (u8 *)tag + tag->size) {
                if (entry->type == MULTIBOOT2_MMAP_RAM) {
                    u64 start = entry->base_addr;
                    u64 end = entry->base_addr + entry->length;

                    if (end > PHYS_LOAD_BASE && start < DIRECT_MAP_SIZE) {
                        if (start < PHYS_LOAD_BASE)
                            start = PHYS_LOAD_BASE;
                        if (end > DIRECT_MAP_SIZE)
                            end = DIRECT_MAP_SIZE;

                        if (nruns && start <= runs[nruns - 1].end) {
                            if (end > runs[nruns - 1].end)
                                runs[nruns - 1].end = end;
                        } else if (nruns < 32) {
                            runs[nruns].start = start;
                            runs[nruns].end = end;
                            nruns++;
                        }
                    }
                }
                entry = (struct multiboot2_mmap_entry *)
                    ((u8 *)entry + mmap_tag->entry_size);
            }
            break;
        }
case MULTIBOOT2_TAG_FRAMEBUFFER: {
            /* Multiboot2 FRAMEBUFFER tag data (after 8-byte tag header):
             *   u32 framebuffer_type   (offset 0)
             *   u64 framebuffer_addr   (offset 4)
             *   u32 framebuffer_pitch  (offset 12)
             *   u16 framebuffer_width  (offset 16)
             *   u16 framebuffer_height (offset 18)
             *   u16 framebuffer_bpp    (offset 20)
             *   ...color masks follow...
             */
            u8 *d = (u8 *)tag + 8;
            u16 w = *(u16 *)(d + 16);
            u16 h = *(u16 *)(d + 18);
            u16 bpp = *(u16 *)(d + 20);
            u32 pitch = *(u32 *)(d + 12);
            u64 addr;
            u8 *pa = (u8 *)&addr;
            pa[0] = d[4]; pa[1] = d[5]; pa[2] = d[6]; pa[3] = d[7];
            pa[4] = d[8]; pa[5] = d[9]; pa[6] = d[10]; pa[7] = d[11];
            pr_info("mbi: framebuffer %ux%u @ %p, %ubpp pitch %u\n",
                    w, h, (void *)addr, bpp, pitch);
            if (addr && w && h) {
                fb_init_early(addr, w, h, pitch, bpp, 1,
                              *(u16 *)(d + 22),
                              *(u16 *)(d + 24),
                              *(u16 *)(d + 26),
                              *(u16 *)(d + 28),
                              *(u16 *)(d + 30),
                              *(u16 *)(d + 32));
            }
            break;
        }
        default:
            break;
        }
    }

    if (!nruns) {
        pr_warn("mbi: no usable RAM regions in memory map\n");
        return;
    }

    pr_info("mbi: usable RAM %llx - %llx (%llu KiB)\n",
            (u64)runs[0].start, (u64)runs[nruns - 1].end,
            (u64)((runs[nruns - 1].end - runs[0].start) >> 10));

    pmm_allocator_init_ranges(runs, nruns);
}