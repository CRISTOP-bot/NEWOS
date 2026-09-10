#include <kernel/pmm.h>
#include <kernel/printk.h>
#include <kernel/kernel.h>
#include <libk/bitmap.h>
#include <libk/string.h>

/* Bitmap-backed frame allocator.
 *
 * Frames are 4 KiB. The bitmap array lives in a fixed buffer sized to
 * CONFIG_PMM_MAX_FRAMES (256 MiB / 4 KiB default), which bounds addressable
 * RAM in this early configuration. */

#ifndef CONFIG_PMM_MAX_FRAMES
#define CONFIG_PMM_MAX_FRAMES (256u * 1024u * 1024u / 4096u)  /* 256 MiB */
#endif

static size_t g_bitmap[BITMAP_WORDS(CONFIG_PMM_MAX_FRAMES)];
static u64    g_base_frame = 0;      /* first tracked physical frame  */
static u64    g_total_frames = 0;
static u64    g_free_frames = 0;
static u64    g_allocated_frames = 0;
static int    g_initialized = 0;

int pmm_is_initialized(void)
{
    return g_initialized;
}

u64 pmm_total_frames(void) { return g_total_frames; }
u64 pmm_free_frames(void)  { return g_free_frames; }
u64 pmm_used_frames(void)  { return g_total_frames - g_free_frames; }

#define pmm_frame_to_bit(f)  ((f) - g_base_frame)
#define pmm_bit_to_frame(i)  ((i) + g_base_frame)

void pmm_init(u64 mem_start, u64 mem_end)
{
    if (mem_end <= mem_start) {
        pr_warn("pmm: invalid range %llx-%llx, skipping\n",
            (u64)mem_start, (u64)mem_end);
        return;
    }

    mem_start = ALIGN_UP(mem_start, PAGE_SIZE);
    mem_end   = ALIGN_DOWN(mem_end, PAGE_SIZE);

    g_base_frame = mem_start >> PAGE_SHIFT;
    u64 total = (mem_end - mem_start) >> PAGE_SHIFT;

    if (total > CONFIG_PMM_MAX_FRAMES)
        total = CONFIG_PMM_MAX_FRAMES;

    bitmap_clear_all(g_bitmap, CONFIG_PMM_MAX_FRAMES);
    g_total_frames = total;
    g_free_frames  = total;
    g_allocated_frames = 0;
    g_initialized = 1;

    pr_info("PMM: tracking %llu frames (%llu MiB) from physical %llx\n",
            (u64)g_total_frames, (u64)((g_total_frames * PAGE_SIZE) >> 20),
            (u64)mem_start);
}

int pmm_frame_alloc(u64 *frame_out)
{
    if (!g_initialized)
        return -1;

    size_t bit = bitmap_find_first_bit(g_bitmap, g_total_frames, 0);
    if (bit >= g_total_frames)
        return -1;

    bitmap_set(g_bitmap, bit, 1);
    g_free_frames--;
    g_allocated_frames++;

    *frame_out = pmm_bit_to_frame(bit);
    return 0;
}

void pmm_frame_free(u64 frame)
{
    if (!g_initialized || frame < g_base_frame)
        return;

    size_t bit = pmm_frame_to_bit(frame);
    if (bit >= g_total_frames)
        return;

    if (!bitmap_get(g_bitmap, bit))
        return;                 /* double free: ignore in early phase */

    bitmap_set(g_bitmap, bit, 0);
    g_free_frames++;
    g_allocated_frames--;
}

static int block_is_free(u64 start_bit, int order)
{
    size_t count = (size_t)1 << order;
    for (size_t i = 0; i < count; i++) {
        if (bitmap_get(g_bitmap, start_bit + i))
            return 0;
    }
    return 1;
}

int pmm_block_alloc(int order, u64 *frame_out)
{
    if (!g_initialized)
        return -1;

    size_t count = (size_t)1 << order;
    if (count > g_total_frames)
        return -1;

    /* Linear scan for a run of free frames. Backend can be upgraded to a
     * buddy structure later (mm/pmm/buddy.c). */
    size_t bit = 0;
    while (bit + count <= g_total_frames) {
        if (block_is_free(bit, order)) {
            for (size_t i = 0; i < count; i++)
                bitmap_set(g_bitmap, bit + i, 1);
            g_free_frames -= count;
            g_allocated_frames += count;
            *frame_out = pmm_bit_to_frame(bit);
            return 0;
        }
        bit++;
    }
    return -1;
}

void pmm_block_free(u64 frame, int order)
{
    if (!g_initialized || frame < g_base_frame)
        return;

    size_t bit = pmm_frame_to_bit(frame);
    size_t count = (size_t)1 << order;
    if (bit + count > g_total_frames)
        return;

    for (size_t i = 0; i < count; i++) {
        if (bitmap_get(g_bitmap, bit + i)) {
            bitmap_set(g_bitmap, bit + i, 0);
            g_free_frames++;
            g_allocated_frames--;
        }
    }
}

void pmm_reserve_region(u64 start, u64 end)
{
    if (!g_initialized)
        return;

    u64 f0 = ALIGN_DOWN(start, PAGE_SIZE) >> PAGE_SHIFT;
    u64 f1 = ALIGN_UP(end, PAGE_SIZE) >> PAGE_SHIFT;

    for (u64 f = f0; f < f1; f++) {
        size_t bit = pmm_frame_to_bit(f);
        if (bit >= g_total_frames)
            continue;
        if (!bitmap_get(g_bitmap, bit)) {
            bitmap_set(g_bitmap, bit, 1);
            g_free_frames--;
            g_allocated_frames++;
        }
    }
}

void pmm_release_region(u64 start, u64 end)
{
    if (!g_initialized)
        return;

    u64 f0 = ALIGN_DOWN(start, PAGE_SIZE) >> PAGE_SHIFT;
    u64 f1 = ALIGN_UP(end, PAGE_SIZE) >> PAGE_SHIFT;

    for (u64 f = f0; f < f1; f++) {
        size_t bit = pmm_frame_to_bit(f);
        if (bit >= g_total_frames)
            continue;
        if (bitmap_get(g_bitmap, bit)) {
            bitmap_set(g_bitmap, bit, 0);
            g_free_frames++;
            g_allocated_frames--;
        }
    }
}

int pmm_frame_is_used(u64 frame)
{
    if (!g_initialized || frame < g_base_frame)
        return 1;

    size_t bit = pmm_frame_to_bit(frame);
    if (bit >= g_total_frames)
        return 1;

    return bitmap_get(g_bitmap, bit);
}

void pmm_dump(void)
{
    pr_info("PMM: total=%llu free=%llu used=%llu frames\n",
            pmm_total_frames(), pmm_free_frames(), pmm_used_frames());
}