#ifndef KERNEL_VMM_H
#define KERNEL_VMM_H

#include <core/core_types.h>
#include <core/core.h>

/* Virtual Memory Manager.
 *
 * Address-space abstraction over the x86_64 page tables. The kernel runs in
 * one global address space (g_kernel_space, backed by the boot trampoline's
 * PML4); every additional address space is built from fresh PML4/frames
 * whose kernel half (PML4 indices 256..511) is shared by value with the
 * kernel space, so the direct physical map and the kernel image remain
 * reachable inside every user address space.
 *
 * Mapping flags (used for the user-visible permission model):
 *   1 = present, 2 = writable, 4 = user, 8 = not-executable (NX) */
#define VMM_PRESENT 1
#define VMM_WRITE   2
#define VMM_USER    4
#define VMM_NX      8

struct vmm_address_space {
    u64 cr3;                /* physical address of the PML4 */
    uintptr_t kernel_base;  /* high-half base of kernel mappings */
    uintptr_t user_base;    /* user region [user_base, user_end) */
    uintptr_t user_end;
    const char *name;
};

void vmm_init(void);
struct vmm_address_space *vmm_kernel_space(void);

/* Address-space lifecycle. */
int  vmm_address_space_create(const char *name,
                              struct vmm_address_space **out);
void vmm_address_space_destroy(struct vmm_address_space *as);

/* Map `count` pages of physical `phys` at virtual `virt`, or map a single
 * freshly allocated (zeroed) page at `virt`. Kernel-half mappings are
 * rejected: the kernel half is shared and managed by the arch layer. */
int vmm_map_pages(struct vmm_address_space *as,
                  uintptr_t virt, uintptr_t phys,
                  u64 count, u32 flags);
int vmm_unmap_pages(struct vmm_address_space *as,
                    uintptr_t virt, u64 count);
int vmm_alloc_page(struct vmm_address_space *as, uintptr_t virt, u32 flags);

/* Lookup: returns 0 if the (leaf) mapping is absent. */
int vmm_page_lookup(struct vmm_address_space *as, uintptr_t virt,
                    uintptr_t *phys_out, u32 *flags_out);

/* Validate that [start, start+len) is fully mapped, user-accessible and
 * inside the user region of `as`. Returns 0 when valid. */
int vmm_validate_user_range(struct vmm_address_space *as,
                            uintptr_t start, size_t len);

/* Activate an address space (load CR3) and query the active one. */
int vmm_switch_to(struct vmm_address_space *as);
struct vmm_address_space *vmm_current(void);

void vmm_dump_address_space(struct vmm_address_space *as);

#endif