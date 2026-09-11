#include <abi/limine.h>
#include <core/core.h>
#include <core/core_bootinfo.h>
#include <core/core_printk.h>
#include <mm/mm_pmm.h>
#include <x86_cpu.h>
#include <x86_gdt.h>
#include <x86_idt.h>
#include <x86_mmu.h>
#include <x86_vendor.h>
#include <iru_string.h>

/* Limine boot protocol entry point.
 *
 * Limine enters the executable in 64-bit long mode with paging enabled and
 * the kernel image mapped at its high-half virtual addresses. This unit
 * consumes the Limine responses while Limine's page tables are active:
 *
 *   1. Grab the executable-address (uniform virtual<->physical offset) and
 *      the memory map, plus informational responses.
 *   2. Install the kernel's own page tables (identity low 1 GiB, direct
 *      physical map at DIRECT_PHYS_BASE, high-half kernel) with the kernel
 *      mapped at its true physical base, and switch CR3.
 *   3. Walk the normal kernel boot tail exactly like the multiboot2/PVH path.
 *
 * The native trampoline (entry.S) is not used here; Limine already has the
 * kernel mapped, so this entry only sets up a stack and calls C.
 */

void kernel_boot_tail(void);
void _limine_entry(void);

/* The command line pointer from Limine points into bootloader-owned memory
 * that is out of reach once we install our own page tables; copy it early. */
static char limine_cmdline[256];

__attribute__((section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_entry_point_request entry_point_request = {
    .id = LIMINE_ENTRY_POINT_REQUEST,
    .revision = 0, .response = NULL,
    .entry = _limine_entry
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_bootloader_info_request
        bootloader_info_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST,
    .revision = 0, .response = NULL
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_firmware_type_request
        firmware_type_request = {
    .id = LIMINE_FIRMWARE_TYPE_REQUEST,
    .revision = 0, .response = NULL
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0, .response = NULL
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0, .response = NULL
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_executable_address_request
        exec_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST,
    .revision = 0, .response = NULL
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_executable_cmdline_request
        cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST,
    .revision = 0, .response = NULL
};

__attribute__((used, section(".limine_requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER;

static u64 limine_virt_base;
static u64 limine_phys_base;

/* The Limine executable image is mapped with a single uniform offset:
 * phys(symbol) = symbol - virtual_base + physical_base. */
static inline uintptr_t limine_phys_of(uintptr_t virt)
{
    return (uintptr_t)(virt - limine_virt_base + limine_phys_base);
}

static void limine_build_paging(void)
{
    const u64 rw = X64_PAGE_PRESENT | X64_PAGE_WRITE;

    for (u64 t = 0; t < X64_PTES; t++) {
        x64_pml4[t] = 0;
        x64_pdpt_low[t] = 0;
        x64_pdpt_high[t] = 0;
        x64_pd[t] = 0;
        x64_pd_kernel[t] = 0;
        x64_pt_kernel[t] = 0;
        x64_pt_kernel1[t] = 0;
        x64_pt_kernel2[t] = 0;
        x64_pdpt_direct[t] = 0;
    }

    /* PML4 wiring: identity (0), direct physical map (508), high half. */
    x64_pml4[0] = limine_phys_of((uintptr_t)x64_pdpt_low) | rw;
    x64_pml4[X64_PDIRECT_INDEX] =
        limine_phys_of((uintptr_t)x64_pdpt_direct) | rw;
    x64_pml4[X64_PKERNEL_INDEX] =
        limine_phys_of((uintptr_t)x64_pdpt_high) | rw;

    /* PDPT wiring: identity index 0, kernel at index 510. */
    x64_pdpt_low[0] = limine_phys_of((uintptr_t)x64_pd) | rw;
    x64_pdpt_high[510] = limine_phys_of((uintptr_t)x64_pd_kernel) | rw;

    /* Kernel PD -> three 4 KiB page tables (6 MiB window). */
    x64_pd_kernel[0] = limine_phys_of((uintptr_t)x64_pt_kernel) | rw;
    x64_pd_kernel[1] = limine_phys_of((uintptr_t)x64_pt_kernel1) | rw;
    x64_pd_kernel[2] = limine_phys_of((uintptr_t)x64_pt_kernel2) | rw;

    /* Identity: 512 x 2 MiB pages mapping physical 0 .. 1 GiB. */
    for (u64 i = 0; i < X64_PTES; i++)
        x64_pd[i] = ((u64)i << X64_PD_SHIFT) | rw | X64_PAGE_HUGE;

    /* Direct map: 4 x 1 GiB pages mapping physical 0 .. 4 GiB. */
    for (int i = 0; i < 4; i++)
        x64_pdpt_direct[i] =
            ((u64)i << X64_PDPT_SHIFT) | rw | X64_PAGE_HUGE;

    /* Kernel image: map KERNEL_BASE_VA .. +6 MiB onto the loader-provided
     * physical base (which is not PHYS_LOAD_BASE under Limine). */
    u64 *pts[3] = { x64_pt_kernel, x64_pt_kernel1, x64_pt_kernel2 };
    for (int p = 0; p < 3; p++) {
        for (u64 i = 0; i < X64_PTES; i++) {
            pts[p][i] = (g_kernel_phys_base + ((u64)p * X64_PTES + i) *
                         PAGE_SIZE) | X64_PAGE_PRESENT | X64_PAGE_WRITE;
        }
    }
}

/* Make the direct physical map live in the current (Limine-provided) page
 * tables so printk()/MMIO work before we switch to our own tables. The
 * active PML4 is reached through the higher-half direct map offset. */
static void limine_warmup_direct_map(u64 hhdm_offset)
{
    const u64 rw = X64_PAGE_PRESENT | X64_PAGE_WRITE;

    for (int i = 0; i < 4; i++)
        x64_pdpt_direct[i] =
            ((u64)i << X64_PDPT_SHIFT) | rw | X64_PAGE_HUGE;

    u64 *active_pml4 = (u64 *)(uintptr_t)(read_cr3() + hhdm_offset);
    active_pml4[X64_PDIRECT_INDEX] =
        limine_phys_of((uintptr_t)x64_pdpt_direct) | rw;
}

void limine_arch_main(void)
{
    struct limine_executable_address_response *ea = exec_address_request.response;
    struct limine_memmap_response *mm = memmap_request.response;
    struct limine_hhdm_response *hhdm = hhdm_request.response;

    /* These are all requested (base revision 3); their responses must be
     * present or we cannot safely take over paging. */
    if (!ea || !mm || !hhdm) {
        for (;;)
            __asm__ volatile("cli; hlt");
    }

    limine_virt_base = ea->virtual_base;
    limine_phys_base = ea->physical_base;
    g_kernel_phys_base = limine_phys_of((uintptr_t)KERNEL_BASE_VA);

    /* Collect the usable RAM runs while Limine's tables (and their mapping
     * of the response data) are still active. Clamped to the direct map. */
    struct pmm_ram_range runs[32];
    int nruns = 0;

    for (u64 i = 0; i < mm->entry_count && nruns < 32; i++) {
        struct limine_memmap_entry *e = mm->entries[i];
        if (e->type != LIMINE_MEMMAP_USABLE)
            continue;

        u64 start = e->base;
        u64 end = e->base + e->length;

        if (end <= PHYS_LOAD_BASE || start >= DIRECT_MAP_SIZE)
            continue;
        if (start < PHYS_LOAD_BASE)
            start = PHYS_LOAD_BASE;
        if (end > DIRECT_MAP_SIZE)
            end = DIRECT_MAP_SIZE;

        if (nruns && start <= runs[nruns - 1].end) {
            if (end > runs[nruns - 1].end)
                runs[nruns - 1].end = end;
        } else {
            runs[nruns].start = start;
            runs[nruns].end = end;
            nruns++;
        }
    }

    /* Warm up the direct map so serial/VGA console output can start, then
     * let printk report the boot environment. */
    limine_warmup_direct_map(hhdm->offset);

    /* Copy the kernel command line into a kernel buffer before Limine's
     * memory is out of reach (pointer values reference Limine's HHDM). */
    {
        struct limine_executable_cmdline_response *cr =
            cmdline_request.response;
        if (cr && cr->cmdline) {
            size_t n = strlen(cr->cmdline);
            if (n >= sizeof(limine_cmdline))
                n = sizeof(limine_cmdline) - 1;
            memcpy(limine_cmdline, cr->cmdline, n);
            limine_cmdline[n] = '\0';
            boot_capture_cmdline(limine_cmdline);
        }
    }

    printk_init();
    printk("NEWOS x86_64 boot: Limine boot protocol\n");

    struct limine_bootloader_info_response *bi =
        bootloader_info_request.response;
    if (bi)
        pr_info("limine: bootloader %s v%s\n", bi->name, bi->version);

    struct limine_firmware_type_response *ft =
        firmware_type_request.response;
    if (ft)
        pr_info("limine: firmware type %llu\n", (u64)ft->firmware_type);

    pr_info("limine: hhdm offset %p, kernel phys base %p\n",
            (void *)hhdm->offset, (void *)g_kernel_phys_base);
    pr_info("limine: %d memory map entries, %d usable run(s)\n",
            (int)mm->entry_count, nruns);

    /* Install our own page tables and switch CR3. From here on the layout is
     * identical to the native boot path (identity + direct + high kernel). */
    limine_build_paging();
    write_cr3(limine_phys_of((uintptr_t)x64_pml4));

    /* CPU and structures. */
    static u8 limine_stack[16384] __attribute__((aligned(16)));
    x64_gdt_init();
    x64_tss_install((u64)(uintptr_t)&limine_stack[sizeof(limine_stack)]);
    x64_idt_init();
    cpu_sti();

    cpu_vendor_init();
    x64_paging_init();

    if (!nruns) {
        pr_warn("limine: no usable RAM regions in memory map\n");
    } else {
        pr_info("limine: usable RAM %llx - %llx (%llu KiB)\n",
                (u64)runs[0].start, (u64)runs[nruns - 1].end,
                (u64)((runs[nruns - 1].end - runs[0].start) >> 10));
        pmm_allocator_init_ranges(runs, nruns);
    }

    kernel_boot_tail();
}