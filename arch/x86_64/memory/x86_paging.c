#include <x86_mmu.h>
#include <x86_cpu.h>
#include <core/core_printk.h>
#include <core/core.h>
#include <iru_string.h>

/* Early page tables populating code (see arch/x86_64/mmu.h). The tables
 * themselves live in .early_bss and are filled by the boot trampoline;
 * this unit finishes the final kernel mappings on top of them. */

#define EFER_MSR          0xC0000080
#define EFER_NXE          (1ull << 11)

void x64_paging_init(void)
{
    /* The trampoline already set up the identity + high-half + direct
     * physical maps. Enable EFER.NXE so the NX bit (bit 63) is honored;
     * user mappings rely on it to keep data non-executable. */
    u32 lo, hi;
    rdmsr(EFER_MSR, &lo, &hi);
    u64 efer = ((u64)hi << 32) | lo;
    efer |= EFER_NXE;
    wrmsr(EFER_MSR, (u32)efer, (u32)(efer >> 32));

    /* The direct physical map (phys_to_virt()) must be live before any C
     * code dereferences a physical address. Verify the trampoline wiring:
     * translate the PML4's address to physical and back through the direct
     * map (under Limine the tables are high-half linked, not identity). */
    uintptr_t pml4_phys = virt_to_phys((uintptr_t)x64_pml4);
    u64 *pml4 = (u64 *)phys_to_virt(pml4_phys);
    if (!(pml4[X64_PDIRECT_INDEX] & X64_PAGE_PRESENT))
        printk("MMU: WARNING direct map PML4 slot not present\n");

    uintptr_t cr3 = read_cr3();
    printk("MMU: long mode paging active, CR3=%p (NX=%d)\n", cr3,
           (int)!!(efer & EFER_NXE));
}

void x64_paging_dump(void)
{
    int mapped = 0;
    for (int i = 0; i < 512; i++) {
        if (x64_pd[i] & X64_PAGE_PRESENT)
            mapped++;
    }
    printk("MMU: %d x 2MiB pages identity mapped at low 1GiB\n", mapped);
}

void arch_mm_init(void)
{
    x64_paging_init();
}