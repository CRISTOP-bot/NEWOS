#include <kernel/pmm.h>
#include <kernel/kernel.h>

/* Physical frame API.
 *
 * Thin wrappers over the pmm backend (bitmap allocator). The bitmap treats
 * a frame bit as "in use"; reserved regions and live allocations share that
 * bit, so phys_is_reserved() reports a frame as reserved when it is not
 * free (i.e. either allocated or explicitly reserved). */

int phys_alloc_frame(u64 *frame_out)
{
    return pmm_frame_alloc(frame_out);
}

void phys_free_frame(u64 frame)
{
    pmm_frame_free(frame);
}

int phys_alloc_block(int order, u64 *frame_out)
{
    return pmm_block_alloc(order, frame_out);
}

void phys_free_block(u64 frame, int order)
{
    pmm_block_free(frame, order);
}

void phys_reserve_range(u64 start, u64 end)
{
    pmm_reserve_region(start, end);
}

void phys_release_range(u64 start, u64 end)
{
    pmm_release_region(start, end);
}

int phys_is_reserved(u64 frame)
{
    return pmm_frame_is_used(frame);
}

u64 phys_total_frames(void)
{
    return pmm_total_frames();
}

u64 phys_free_frames(void)
{
    return pmm_free_frames();
}