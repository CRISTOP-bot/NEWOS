#include <mm/mm_vmm.h>
#include <core/core.h>
#include <mm/mm_pmm.h>
#include <mm/mm_heap.h>
#include <core/core_printk.h>
#include <x86_mmu.h>
#include <x86_cpu.h>
#include <iru_string.h>

/* Address-space lifecycle.
 *
 * The kernel address space is described by the boot trampoline's PML4.
 * New address spaces get a fresh PML4 whose kernel half (indices 256..511)
 * is copied from the kernel space, sharing the direct physical map and the
 * kernel image by value across every user space. */

static struct vmm_address_space g_kernel_space;
static struct vmm_address_space *g_current;

static u64 *x64_table_at(uintptr_t table_phys)
{
    return (u64 *)(uintptr_t)phys_to_virt(table_phys & X64_PAGE_BITS);
}

void vmm_init(void)
{
    g_kernel_space.cr3 = virt_to_phys((uintptr_t)x64_pml4);
    g_kernel_space.kernel_base = KERNEL_BASE_VA;
    g_kernel_space.user_base = 0;
    g_kernel_space.user_end = USER_SPACE_BASE;
    g_kernel_space.name = "kernel";
    g_current = &g_kernel_space;

    printk("VMM: kernel address space ready (CR3=%p, direct map at %p)\n",
           g_kernel_space.cr3, (void *)DIRECT_PHYS_BASE);
}

struct vmm_address_space *vmm_kernel_space(void)
{
    return &g_kernel_space;
}

int vmm_address_space_create(const char *name,
                             struct vmm_address_space **out)
{
    u64 pml4_frame;
    if (pmm_frame_alloc(&pml4_frame) != 0)
        return -1;

    u64 *pml4 = (u64 *)phys_to_virt(pml4_frame << PAGE_SHIFT);
    memset(pml4, 0, PAGE_SIZE);

    struct vmm_address_space *as = kzalloc(sizeof(*as));
    if (!as) {
        pmm_frame_free(pml4_frame);
        return -1;
    }

    /* Share the kernel half (direct map + kernel image) by value. */
    u64 *kernel_pml4 = x64_table_at(g_kernel_space.cr3);
    for (int i = 256; i < 512; i++)
        pml4[i] = kernel_pml4[i];

    as->cr3 = pml4_frame << PAGE_SHIFT;
    as->kernel_base = KERNEL_BASE_VA;
    as->user_base = USER_SPACE_BASE;
    as->user_end = USER_SPACE_END;
    as->name = name;

    *out = as;
    return 0;
}

void vmm_address_space_destroy(struct vmm_address_space *as)
{
    if (!as)
        return;

    /* Free every user mapping, intermediate table and finally the PML4.
     * The kernel half is shared and never reclaimed here. */
    u64 *pml4 = x64_table_at(as->cr3);
    for (u32 i1 = 0; i1 < 256; i1++) {
        if (!(pml4[i1] & X64_PAGE_PRESENT))
            continue;

        u64 *pdpt = x64_table_at(pml4[i1]);
        for (u32 i2 = 0; i2 < X64_PTES; i2++) {
            if (!(pdpt[i2] & X64_PAGE_PRESENT))
                continue;
            if (pdpt[i2] & X64_PAGE_HUGE) {
                pmm_frame_free(pdpt[i2] >> X64_PT_SHIFT);
                continue;
            }

            u64 *pd = x64_table_at(pdpt[i2]);
            for (u32 i3 = 0; i3 < X64_PTES; i3++) {
                if (!(pd[i3] & X64_PAGE_PRESENT))
                    continue;
                if (pd[i3] & X64_PAGE_HUGE) {
                    pmm_frame_free(pd[i3] >> X64_PT_SHIFT);
                    continue;
                }

                u64 *pt = x64_table_at(pd[i3]);
                for (u32 i4 = 0; i4 < X64_PTES; i4++) {
                    if (pt[i4] & X64_PAGE_PRESENT)
                        pmm_frame_free(pt[i4] >> X64_PT_SHIFT);
                }
                pmm_frame_free(pd[i3] >> X64_PT_SHIFT);
            }
            pmm_frame_free(pdpt[i2] >> X64_PT_SHIFT);
        }
        pmm_frame_free(pml4[i1] >> X64_PT_SHIFT);
    }
    pmm_frame_free(as->cr3 >> X64_PT_SHIFT);

    kfree(as);
}

int vmm_switch_to(struct vmm_address_space *as)
{
    if (!as)
        return -1;
    g_current = as;
    write_cr3(as->cr3);
    return 0;
}

struct vmm_address_space *vmm_current(void)
{
    return g_current;
}

void vmm_dump_address_space(struct vmm_address_space *as)
{
    if (!as)
        return;

    int pages = 0;
    u64 *pml4 = x64_table_at(as->cr3);
    for (u32 i1 = 0; i1 < 256; i1++) {
        if (!(pml4[i1] & X64_PAGE_PRESENT))
            continue;
        u64 *pdpt = x64_table_at(pml4[i1]);
        for (u32 i2 = 0; i2 < X64_PTES; i2++) {
            if (!(pdpt[i2] & X64_PAGE_PRESENT))
                continue;
            if (pdpt[i2] & X64_PAGE_HUGE) {
                pages += 256 * 1024;
                continue;
            }
            u64 *pd = x64_table_at(pdpt[i2]);
            for (u32 i3 = 0; i3 < X64_PTES; i3++) {
                if (!(pd[i3] & X64_PAGE_PRESENT))
                    continue;
                if (pd[i3] & X64_PAGE_HUGE) {
                    pages += 512;
                    continue;
                }
                u64 *pt = x64_table_at(pd[i3]);
                for (u32 i4 = 0; i4 < X64_PTES; i4++)
                    if (pt[i4] & X64_PAGE_PRESENT)
                        pages++;
            }
        }
    }
    printk("VMM: address space '%s' CR3=%p user pages ~%d\n",
           as->name, as->cr3, pages);
}