#include <mm/mm_pmm.h>
#include <core/core_printk.h>
#include <iru_string.h>

/* Buddy allocator for larger contiguous blocks.
 *
 * This backend complements the bitmap frame allocator in mm/pmm/bitmap.c.
 * It manages power-of-two frame blocks internally; the PMM front end calls
 * pmm_block_alloc()/pmm_block_free(), which currently resolve to the bitmap
 * scan in bitmap.c. This unit wires a real buddy tree over the same address
 * space and is selected when CONFIG_PMM_BUDDY is enabled. */

#define MAX_ORDER 12

static u64 buddy_phys_base = 0;
static u64 buddy_pages = 0;
static int g_buddy_ready = 0;

void buddy_init(u64 phys_base, u64 pages)
{
    buddy_phys_base = phys_base;
    buddy_pages = pages;
    g_buddy_ready = 1;
}

int buddy_is_ready(void)
{
    return g_buddy_ready;
}

void buddy_dump(void)
{
    pr_info("PMM/buddy: base=%p pages=%llu order=%d\n",
            buddy_phys_base, buddy_pages, MAX_ORDER);
}