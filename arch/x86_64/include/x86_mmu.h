#ifndef ARCH_X86_64_MMU_H
#define ARCH_X86_64_MMU_H

#include <core/core_types.h>
#include <core/core.h>

#define X64_PTES         512ull
#define X64_PAGE_PRESENT (1ull << 0)
#define X64_PAGE_WRITE   (1ull << 1)
#define X64_PAGE_USER    (1ull << 2)
#define X64_PAGE_PWT     (1ull << 3)   /* write-through */
#define X64_PAGE_PCD     (1ull << 4)   /* cache-disable (UC with PWT) */
#define X64_PAGE_ACCESS  (1ull << 5)
#define X64_PAGE_DIRTY   (1ull << 6)
#define X64_PAGE_HUGE    (1ull << 7)
#define X64_PAGE_NX      (1ull << 63)
#define X64_PAGE_BITS    (0xFFFFFFFFF000ull)

#define X64_PML4_SHIFT   39
#define X64_PDPT_SHIFT   30
#define X64_PD_SHIFT     21
#define X64_PT_SHIFT     12

#define X64_PDIRECT_INDEX  508
#define X64_PKERNEL_INDEX  511

static inline u16 x64_pml4_index(uintptr_t va){ return (va >> X64_PML4_SHIFT) & 0x1FF; }
static inline u16 x64_pdpt_index(uintptr_t va){ return (va >> X64_PDPT_SHIFT) & 0x1FF; }
static inline u16 x64_pd_index  (uintptr_t va){ return (va >> X64_PD_SHIFT)   & 0x1FF; }
static inline u16 x64_pt_index  (uintptr_t va){ return (va >> X64_PT_SHIFT)   & 0x1FF; }

extern u64 x64_pml4[512];
extern u64 x64_pdpt_low[512];
extern u64 x64_pdpt_high[512];
extern u64 x64_pd[512];
extern u64 x64_pd_kernel[512];
/* Early kernel-image page tables: X64_KMAP_PTS * 2 MiB of window, filled with
 * 4 KiB pages because neither loader guarantees a 2 MiB-aligned physical base
 * (PHYS_BASE is 1 MiB; Limine is only 4 KiB-aligned). Must stay in step with
 * KERNEL_MAP_PTS in x86_entry.S, which reserves and wires the same array. */
#define X64_KMAP_PTS 32
extern u64 x64_pt_kernel[X64_KMAP_PTS][512];
extern u64 x64_pdpt_direct[512];
/* Direct-map page directories: 4 x 512 x 2 MiB covering phys 0 .. 4 GiB.
 * Deliberately 2 MiB, not 1 GiB: some hypervisors (VirtualBox) hide the
 * PDPE1GB CPUID bit, and a 1 GiB page there faults with a reserved-bit
 * #PF on first touch. 2 MiB pages work everywhere long mode does. */
extern u64 x64_pd_direct[4][512];

void x64_paging_init(void);
void x64_paging_dump(void);

void arch_mm_init(void);

#endif
