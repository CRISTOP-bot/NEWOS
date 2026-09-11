#ifndef NEWOS_KERNEL_BOOTINFO_H
#define NEWOS_KERNEL_BOOTINFO_H

#include <core/core_types.h>

/* QEMU PVH (x86_load_pvh) calling convention.
 *
 * When QEMU boots an ELF kernel that carries a "Xen" ELF note of type
 * XEN_ELFNOTE_PHYS32_ENTRY (18), it enters the entry point in 32-bit
 * protected mode (paging disabled) and passes:
 *     EAX = PVH_MAGIC
 *     EBX = physical address of struct hvm_start_info
 * See xen/include/public/hvm/start_info.h.
 */

#define PVH_MAGIC 0x336ec578u

#define SIF_CMDLINE (1u << 0)
#define SIF_MODS    (1u << 1)
#define SIF_RSDP    (1u << 2)
#define SIF_MEM_MAP (1u << 3)

#define E820_RAM    1

struct hvm_start_info {
    u32 magic;              /* PVH_MAGIC */
    u32 version;            /* layout version, currently 1 */
    u32 flags;              /* SIF_* bits */
    u32 nr_modules;
    u64 mods_paddr;         /* physical */
    u64 cmdline_paddr;      /* physical */
    u64 rsdp_paddr;         /* physical */
    u64 memmap_paddr;       /* physical */
    u32 memmap_entries;
    u32 reserved;
    u64 reserved2;
};

struct hvm_memmap_table_entry {
    u64 addr;
    u64 size;
    u32 type;               /* E820_RAM, ... */
    u32 reserved;
};

/* Boot command line captured by whichever boot protocol was used. */
extern const char *g_boot_cmdline;
void boot_capture_cmdline(const char *cmd);
int boot_cmdline_has(const char *needle);

#endif /* NEWOS_KERNEL_BOOTINFO_H */