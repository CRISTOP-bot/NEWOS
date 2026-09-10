#include <kernel/kmalloc.h>
#include <kernel/printk.h>
#include <libk/string.h>

/* Kernel heap arena. In the early phase the arena is a static BSS array so
 * the heap is available before PMM paging is fully wired; a later phase
 * grows it from PMM frames directly. */

#define KHEAP_ARENA_SIZE (4u * 1024u * 1024u)   /* 4 MiB */

static u8 kheap_arena[KHEAP_ARENA_SIZE] __attribute__((aligned(4096)));

void kheap_init_early(void)
{
    kheap_init(kheap_arena, KHEAP_ARENA_SIZE);
    pr_info("kheap: 4MiB early arena at %p\n", kheap_arena);
}