#include <abi/multiboot2.h>
#include <core/core_bootinfo.h>
#include <core/core_printk.h>
#include <mm/mm_pmm.h>
#include <mm/mm_heap.h>
#include <mm/mm_vmm.h>
#include <fs/vfs.h>
#include <drivers/drv_core.h>
#include <x86_cpu.h>
#include <x86_gdt.h>
#include <x86_idt.h>
#include <x86_mmu.h>
#include <fs/tmpfs.h>
#include <fs/devfs.h>
#include <fs/initramfs.h>
#include <drivers/pit_timer.h>
#include <drivers/serial_16550.h>
#include <drivers/pci.h>
#include <x86_vendor.h>
#include "x86_multiboot2.h"

void pvh_parse_mmap(u64 pvh_phys);
void mbi_parse_mmap(u64 mbi_phys);
void kernel_start(void) __attribute__((noreturn));
void kheap_init_early(void);

/* Physical base of the kernel image; the Limine boot path overrides this
 * with the bootloader-reported executable base. */
uintptr_t g_kernel_phys_base = PHYS_LOAD_BASE;

static u8 boot_stack[16384] __attribute__((aligned(16)));

/* Shared tail of the boot sequence, identical for every boot protocol:
 * memory/vfs/filesystem services, device model + PCI, timers and the final
 * jump into the kernel framework. */
void kernel_boot_tail(void)
{
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
    pci_init();

    /* System services: the PIT tick drives delays (and later the scheduler);
     * COM1 RX becomes interrupt-driven so the console can read input. */
    pit_init(PIT_DEFAULT_HZ);
    serial_rx_irq_enable(SERIAL_COM1);

    /* Enter the kernel framework. */
    kernel_start();
}

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

    cpu_vendor_init();
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

    kernel_boot_tail();
}