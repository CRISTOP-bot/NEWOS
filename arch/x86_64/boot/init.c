#include <kernel/multiboot2.h>
#include <kernel/bootinfo.h>
#include <kernel/printk.h>
#include <kernel/pmm.h>
#include <kernel/kmalloc.h>
#include <kernel/vmm.h>
#include <kernel/vfs.h>
#include <kernel/driver.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/gdt/gdt.h>
#include <arch/x86_64/idt/idt.h>
#include <arch/x86_64/mmu.h>
#include <fs/tmpfs/tmpfs.h>
#include <fs/devfs/devfs.h>
#include <fs/initramfs/initramfs.h>
#include "mbi.h"

void pvh_parse_mmap(u64 pvh_phys);
void mbi_parse_mmap(u64 mbi_phys);
void kernel_start(void) __attribute__((noreturn));
void kheap_init_early(void);

static u8 boot_stack[16384] __attribute__((aligned(16)));

void arch_main(u32 magic, u32 info_phys)
{
    printk_init();
    printk("NEWOS x86_64 boot: entry(magic=%x, info=%x)\n",
           magic, info_phys);

    if (magic == PVH_MAGIC) {
        pr_info("boot: QEMU PVH protocol detected\n");
    } else if (magic == MULTIBOOT2_MAGIC_CHECK) {
        pr_info("boot: multiboot2 protocol detected\n");
    } else if (info_phys &&
               *(u32 *)(uintptr_t)info_phys == PVH_MAGIC) {
        /* Some PVH loaders leave EAX unset; the magic lives inside
         * hvm_start_info. Accept it for robustness. */
        pr_info("boot: PVH protocol detected (magic probed in start info)\n");
        magic = PVH_MAGIC;
    } else {
        pr_warn("boot: magic mismatch (0x%x) - continuing anyway\n",
                magic);
    }

    /* CPU and structures. */
    x64_gdt_init();
    x64_tss_install((u64)(uintptr_t)&boot_stack[sizeof(boot_stack)]);
    x64_idt_init();
    cpu_sti();

    cpu_print_info();
    x64_paging_init();

    /* Physical memory from the boot memory map. */
    if (info_phys) {
        if (magic == PVH_MAGIC)
            pvh_parse_mmap(info_phys);
        else
            mbi_parse_mmap(info_phys);
    } else {
        pr_warn("boot: no info pointer, PMM left uninitialized\n");
    }

    /* Virtual memory manager (direct map + kernel address space). */
    vmm_init();

    /* Early heap, VFS, filesystems. */
    kheap_init_early();
    vfs_init();
    tmpfs_init();
    devfs_init();
    initramfs_init();

    /* Device model. */
    device_model_init();

    /* Enter the kernel framework. */
    kernel_start();
}