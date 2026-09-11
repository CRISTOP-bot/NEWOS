#include <core/core_printk.h>
#include <mm/mm_pmm.h>
#include <mm/mm_heap.h>
#include <core/core.h>

/* Memory debugging helpers. Counts live allocations and reports PMM and
 * heap usage. A future canary/pool implementation lives here too; for now
 * track accounting only. */

#define MEMDEBUG_CANARY 0xCAFE

static u64 g_allocations = 0;
static u64 g_total_bytes = 0;

int memory_debug_enabled(void)
{
    return 1;
}

void memory_debug_track(void *ptr, size_t size)
{
    if (!ptr)
        return;
    g_allocations++;
    g_total_bytes += size;
}

int memory_debug_verify(const void *ptr, size_t size)
{
    (void)ptr;
    (void)size;
    return 1;
}

void memory_debug_dump(void)
{
    pr_info("memdebug: %llu live allocations, %llu bytes tracked\n",
            g_allocations, g_total_bytes);

    u64 total = pmm_total_frames() * PAGE_SIZE;
    u64 used  = pmm_used_frames() * PAGE_SIZE;
    pr_info("memdebug: PMM total=%lluMiB used=%lluMiB\n",
            total >> 20, used >> 20);
    kheap_dump();
}