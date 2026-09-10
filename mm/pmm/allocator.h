#ifndef MM_PMM_ALLOCATOR_H
#define MM_PMM_ALLOCATOR_H

#include <kernel/types.h>

/* Front-end init that wires the backend(s) to the multiboot memory map. */
void pmm_allocator_init(u64 mem_start, u64 mem_end);

#endif