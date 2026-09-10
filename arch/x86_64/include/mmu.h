#ifndef ARCH_X86_64_MMU_H
#define ARCH_X86_64_MMU_H

#include <kernel/types.h>
#include <kernel/kernel.h>

#define X64_PTES         512ull
#define X64_PAGE_PRESENT (1ull << 0)
#define X64_PAGE_WRITE   (1ull << 1)
#define X64_PAGE_USER    (1ull << 2)
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
extern u64 x64_pt_kernel[512];
extern u64 x64_pdpt_direct[512];

void x64_paging_init(void);
void x64_paging_dump(void);

#endif
