#ifndef MM_PMM_H
#define MM_PMM_H

#include <core/core_types.h>

/* Physical Memory Manager.
 *
 * The PMM tracks all usable physical memory from the boot loader's memory
 * map. Frame allocation is provided by a bitmap backend with a buddy
 * allocator for larger orders (backend selected at boot by the front-end). */

/* One usable physical RAM run reported by the boot loader's memory map. */
struct pmm_ram_range {
    u64 start;
    u64 end;
};

/* Front-end init: wires the backend(s) to the boot memory map (multiboot2 /
 * PVH). Called by the machine boot path once the loader's memory map is
 * known. pmm_allocator_init_ranges() masks the whole interesting window
 * and releases exactly the RAM runs, so MMIO holes stay reserved. */
void     pmm_allocator_init(u64 mem_start, u64 mem_end);
int      pmm_allocator_init_ranges(const struct pmm_ram_range *runs,
                                   int count);

void     pmm_init(u64 mem_start, u64 mem_end);
int      pmm_is_initialized(void);

/* Total tracked memory and frame counts. */
u64      pmm_total_frames(void);
u64      pmm_free_frames(void);
u64      pmm_used_frames(void);

/* Allocate/release single 4 KiB frames. Return 0 on success, -1 on OOM. */
int      pmm_frame_alloc(u64 *frame_out);
void     pmm_frame_free(u64 frame);

/* Allocate a physically contiguous run of `order` frames (2^order pages). */
int      pmm_block_alloc(int order, u64 *frame_out);
void     pmm_block_free(u64 frame, int order);

/* Reserve a region so the allocator never hands it out. */
void     pmm_reserve_region(u64 start, u64 end);
void     pmm_release_region(u64 start, u64 end);
int      pmm_frame_is_used(u64 frame);

/* Debug: report allocator state. */
void     pmm_dump(void);

/* -------------------------------------------------------------------------
 * Physical frame API (thin wrappers over the pmm backend; the process/VMM
 * layers use these so the allocator backend can be swapped underneath). */
int      phys_alloc_frame(u64 *frame_out);
void     phys_free_frame(u64 frame);
int      phys_alloc_block(int order, u64 *frame_out);
void     phys_free_block(u64 frame, int order);
void     phys_reserve_range(u64 start, u64 end);
void     phys_release_range(u64 start, u64 end);
int      phys_is_reserved(u64 frame);
u64      phys_total_frames(void);
u64      phys_free_frames(void);

#define PMM_INVALID_FRAME (~(u64)0)

#endif