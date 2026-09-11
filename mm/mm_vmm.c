#include <mm/mm_vmm.h>
#include <core/core.h>
#include <mm/mm_pmm.h>
#include <core/core_printk.h>
#include <x86_mmu.h>
#include <x86_cpu.h>
#include <iru_string.h>

/* Page-table walker and mapping primitives.
 *
 * All tables are reached through the direct physical map (phys_to_virt()),
 * so mapping operations never need to switch CR3: they manipulate the
 * in-memory table hierarchy of the target address space directly. */

static u64 *x64_table_at(uintptr_t table_phys)
{
    return (u64 *)(uintptr_t)phys_to_virt(table_phys & X64_PAGE_BITS);
}

static int x64_allocate_table(u64 *entry_out)
{
    u64 frame;
    if (pmm_frame_alloc(&frame) != 0)
        return -1;

    u64 *table = (u64 *)phys_to_virt(frame << PAGE_SHIFT);
    memset(table, 0, PAGE_SIZE);

    /* Intermediate entries need U/S set so user-mode walks can traverse
     * them; this routine is only ever used for user-half tables (kernel
     * half mappings are rejected above). */
    *entry_out = (frame << PAGE_SHIFT) | X64_PAGE_PRESENT | X64_PAGE_WRITE |
                 X64_PAGE_USER;
    return 0;
}

static u64 x64_flags_to_pte(u32 flags)
{
    u64 pte = X64_PAGE_PRESENT;
    if (flags & VMM_WRITE)
        pte |= X64_PAGE_WRITE;
    if (flags & VMM_USER)
        pte |= X64_PAGE_USER;
    if (flags & VMM_NX)
        pte |= X64_PAGE_NX;
    return pte;
}

static u32 x64_pte_to_flags(u64 pte)
{
    u32 flags = 0;
    if (pte & X64_PAGE_PRESENT)
        flags |= VMM_PRESENT;
    if (pte & X64_PAGE_WRITE)
        flags |= VMM_WRITE;
    if (pte & X64_PAGE_USER)
        flags |= VMM_USER;
    if (pte & X64_PAGE_NX)
        flags |= VMM_NX;
    return flags;
}

int vmm_map_pages(struct vmm_address_space *as, uintptr_t virt,
                  uintptr_t phys, u64 count, u32 flags)
{
    if (!as || !count)
        return -1;

    for (u64 i = 0; i < count; i++) {
        uintptr_t va = virt + i * PAGE_SIZE;
        uintptr_t pa = phys + i * PAGE_SIZE;

        u16 idx1 = x64_pml4_index(va);
        if (idx1 >= 256) {
            /* The kernel half is shared and managed by the arch layer. */
            pr_warn("vmm: kernel-half mapping rejected (%p)\n", va);
            return -1;
        }

        u64 *pml4 = x64_table_at(as->cr3);
        if (!(pml4[idx1] & X64_PAGE_PRESENT)) {
            if (x64_allocate_table(&pml4[idx1]) != 0)
                return -1;
        }

        u64 *pdpt = x64_table_at(pml4[idx1]);
        u16 idx2 = x64_pdpt_index(va);
        if (pdpt[idx2] & X64_PAGE_HUGE)      /* 1 GiB page in the way */
            return -1;
        if (!(pdpt[idx2] & X64_PAGE_PRESENT)) {
            if (x64_allocate_table(&pdpt[idx2]) != 0)
                return -1;
        }

        u64 *pd = x64_table_at(pdpt[idx2]);
        u16 idx3 = x64_pd_index(va);
        if (pd[idx3] & X64_PAGE_HUGE)        /* 2 MiB page in the way */
            return -1;
        if (!(pd[idx3] & X64_PAGE_PRESENT)) {
            if (x64_allocate_table(&pd[idx3]) != 0)
                return -1;
        }

        u64 *pt = x64_table_at(pd[idx3]);
        u16 idx4 = x64_pt_index(va);
        if (pt[idx4] & X64_PAGE_PRESENT)
            return -1;                        /* already mapped */

        pt[idx4] = (pa & X64_PAGE_BITS) | x64_flags_to_pte(flags);
    }
    return 0;
}

int vmm_alloc_page(struct vmm_address_space *as, uintptr_t virt, u32 flags)
{
    u64 frame;
    if (pmm_frame_alloc(&frame) != 0)
        return -1;

    u64 *page = (u64 *)phys_to_virt(frame << PAGE_SHIFT);
    memset(page, 0, PAGE_SIZE);

    if (vmm_map_pages(as, virt, frame << PAGE_SHIFT, 1, flags) != 0) {
        pmm_frame_free(frame);
        return -1;
    }
    return 0;
}

int vmm_unmap_pages(struct vmm_address_space *as, uintptr_t virt, u64 count)
{
    if (!as)
        return -1;

    for (u64 i = 0; i < count; i++) {
        uintptr_t va = virt + i * PAGE_SIZE;

        u16 idx1 = x64_pml4_index(va);
        u64 *pml4 = x64_table_at(as->cr3);
        if (!(pml4[idx1] & X64_PAGE_PRESENT))
            continue;

        u64 *pdpt = x64_table_at(pml4[idx1]);
        u16 idx2 = x64_pdpt_index(va);
        if (!(pdpt[idx2] & X64_PAGE_PRESENT) ||
            (pdpt[idx2] & X64_PAGE_HUGE))
            continue;

        u64 *pd = x64_table_at(pdpt[idx2]);
        u16 idx3 = x64_pd_index(va);
        if (!(pd[idx3] & X64_PAGE_PRESENT) ||
            (pd[idx3] & X64_PAGE_HUGE))
            continue;

        u64 *pt = x64_table_at(pd[idx3]);
        u16 idx4 = x64_pt_index(va);
        if (pt[idx4] & X64_PAGE_PRESENT) {
            pt[idx4] = 0;
            if (as == vmm_current())
                invlpg(va);
        }
    }
    return 0;
}

int vmm_page_lookup(struct vmm_address_space *as, uintptr_t virt,
                    uintptr_t *phys_out, u32 *flags_out)
{
    if (!as)
        return 0;

    u16 idx1 = x64_pml4_index(virt);
    u64 *pml4 = x64_table_at(as->cr3);
    if (!(pml4[idx1] & X64_PAGE_PRESENT))
        return 0;

    u64 *pdpt = x64_table_at(pml4[idx1]);
    u16 idx2 = x64_pdpt_index(virt);
    if (!(pdpt[idx2] & X64_PAGE_PRESENT))
        return 0;
    if (pdpt[idx2] & X64_PAGE_HUGE) {
        if (phys_out)
            *phys_out = (pdpt[idx2] & X64_PAGE_BITS) +
                        (virt & ((1ull << X64_PDPT_SHIFT) - 1));
        if (flags_out) {
            *flags_out = VMM_PRESENT | VMM_WRITE;
            if (pdpt[idx2] & X64_PAGE_USER)
                *flags_out |= VMM_USER;
        }
        return 1;
    }

    u64 *pd = x64_table_at(pdpt[idx2]);
    u16 idx3 = x64_pd_index(virt);
    if (!(pd[idx3] & X64_PAGE_PRESENT))
        return 0;
    if (pd[idx3] & X64_PAGE_HUGE) {
        if (phys_out)
            *phys_out = (pd[idx3] & X64_PAGE_BITS) +
                        (virt & ((1ull << X64_PD_SHIFT) - 1));
        if (flags_out)
            *flags_out = x64_pte_to_flags(pd[idx3] & ~X64_PAGE_HUGE);
        return 1;
    }

    u64 *pt = x64_table_at(pd[idx3]);
    u16 idx4 = x64_pt_index(virt);
    if (!(pt[idx4] & X64_PAGE_PRESENT))
        return 0;

    if (phys_out)
        *phys_out = (pt[idx4] & X64_PAGE_BITS) +
                    (virt & (PAGE_SIZE - 1));
    if (flags_out)
        *flags_out = x64_pte_to_flags(pt[idx4]);
    return 1;
}

int vmm_validate_user_range(struct vmm_address_space *as,
                            uintptr_t start, size_t len)
{
    if (!as)
        return -1;

    if (len == 0)
        return 0;

    uintptr_t end = start + len;
    if (end < start)                    /* overflow */
        return -1;
    if (start < as->user_base || end > as->user_end)
        return -1;

    uintptr_t p = ALIGN_DOWN(start, PAGE_SIZE);
    for (; p < end; p += PAGE_SIZE) {
        u32 flags = 0;
        if (!vmm_page_lookup(as, p, NULL, &flags))
            return -1;
        if (!(flags & VMM_PRESENT) || !(flags & VMM_USER))
            return -1;
    }
    return 0;
}