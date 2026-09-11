#include <mm/mm_heap.h>
#include <core/core.h>
#include <core/core_printk.h>
#include <core/core_panic.h>
#include <iru_string.h>
#include <iru_lock.h>

/* Simple first-fit heap allocator with an embedded free list.
 *
 * Heap layout:
 *
 *   +-------------------+  <- arena
 *   | struct heap_arena |
 *   +-------------------+
 *   | block [hdr][data] | [hdr][data] | ... |
 *   +-------------------+
 *
 * A block header carries `total_size` (including the header) and a `free`
 * flag. The first bytes of a free block's data hold next/prev free-list
 * pointers. Freed blocks are coalesced with a following free neighbor and
 * blocks are split when larger than needed.
 */

#define HEAP_MAGIC       0x4845415020444247ull   /* "HEAP DBG" */
#define HEAP_HDR_MAGIC   0x424C4F434B484452ull   /* "BLOCKHDR" */
#define HEAP_ALIGN       16
#define HEAP_MIN_BLOCK   32

struct heap_header {
    u64 magic;
    size_t total_size;
    int free;
} __attribute__((packed));

struct free_node {
    struct free_node *next;
    struct free_node *prev;
};

struct heap_arena {
    u64 magic;
    u64 *first_free;
    size_t total;
    size_t used;
    struct spinlock lock;
};

static struct heap_arena *g_heap = NULL;

#define HDR_SIZE   (sizeof(struct heap_header))
#define DATA_TO_HDR(p) ((struct heap_header *)((u8 *)(p) - HDR_SIZE))
#define HDR_TO_DATA(h) ((void *)((u8 *)(h) + HDR_SIZE))

void kheap_init(void *arena, size_t size)
{
    size = ALIGN_DOWN(size, HEAP_ALIGN);
    if (size <= sizeof(struct heap_arena))
        panic("kheap: arena too small");

    g_heap = (struct heap_arena *)arena;
    g_heap->magic = HEAP_MAGIC;
    g_heap->first_free = NULL;
    g_heap->total = size;
    g_heap->used = sizeof(struct heap_arena);
    spinlock_init(&g_heap->lock, "kheap");

    struct heap_header *hdr =
        (struct heap_header *)((u8 *)arena + sizeof(struct heap_arena));
    hdr->magic = HEAP_HDR_MAGIC;
    hdr->total_size = size - sizeof(struct heap_arena);
    hdr->free = 1;

    struct free_node *node = (struct free_node *)HDR_TO_DATA(hdr);
    node->next = NULL;
    node->prev = NULL;
    g_heap->first_free = (u64 *)node;
}

static void free_list_remove(struct free_node *node)
{
    if (node->prev)
        node->prev->next = node->next;
    else
        g_heap->first_free = (u64 *)node->next;

    if (node->next)
        node->next->prev = node->prev;

    node->next = NULL;
    node->prev = NULL;
}

static void free_list_insert(struct free_node *node)
{
    node->next = (struct free_node *)g_heap->first_free;
    node->prev = NULL;
    if (node->next)
        node->next->prev = node;
    g_heap->first_free = (u64 *)node;
}

void *kmalloc(size_t size)
{
    if (!g_heap)
        return NULL;

    if (size == 0)
        size = 1;

    spinlock_lock(&g_heap->lock);

    size = ALIGN_UP(size + HDR_SIZE, HEAP_ALIGN);
    size = MAX(size, HDR_SIZE + HEAP_MIN_BLOCK);

    struct free_node *node = (struct free_node *)g_heap->first_free;
    struct heap_header *best = NULL;

    while (node) {
        struct heap_header *hdr = DATA_TO_HDR(node);
        if (hdr->free && hdr->total_size >= size) {
            if (!best || hdr->total_size < best->total_size)
                best = hdr;
            if (hdr->total_size - size < HEAP_MIN_BLOCK + HDR_SIZE)
                break;   /* near-perfect fit */
        }
        node = node->next;
    }

    if (!best) {
        spinlock_unlock(&g_heap->lock);
        pr_warn("kheap: out of memory (requested %u)\n", (unsigned)size);
        return NULL;
    }

    free_list_remove((struct free_node *)HDR_TO_DATA(best));

    if (best->total_size - size >= HDR_SIZE + HEAP_MIN_BLOCK) {
        struct heap_header *next_hdr =
            (struct heap_header *)((u8 *)best + size);
        next_hdr->magic = HEAP_HDR_MAGIC;
        next_hdr->total_size = best->total_size - size;
        next_hdr->free = 1;

        free_list_insert((struct free_node *)HDR_TO_DATA(next_hdr));
        best->total_size = size;
    }

    best->free = 0;
    g_heap->used += best->total_size;

    spinlock_unlock(&g_heap->lock);
    return HDR_TO_DATA(best);
}

void *kzalloc(size_t size)
{
    void *p = kmalloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

static void coalesce_following(struct heap_header *hdr)
{
    struct heap_header *next =
        (struct heap_header *)((u8 *)hdr + hdr->total_size);

    if ((u8 *)next + HDR_SIZE <= (u8 *)g_heap + g_heap->total &&
        next->magic == HEAP_HDR_MAGIC && next->free) {
        free_list_remove((struct free_node *)HDR_TO_DATA(next));
        hdr->total_size += next->total_size;
    }
}

void kfree(void *ptr)
{
    if (!ptr || !g_heap)
        return;

    struct heap_header *hdr = DATA_TO_HDR(ptr);
    spinlock_lock(&g_heap->lock);

    if (hdr->magic != HEAP_HDR_MAGIC)
        panic("kfree: bad block magic at %p", ptr);
    if (hdr->free)
        panic("kfree: double free of %p", ptr);

    hdr->free = 1;
    g_heap->used -= hdr->total_size;

    coalesce_following(hdr);
    free_list_insert((struct free_node *)HDR_TO_DATA(hdr));

    spinlock_unlock(&g_heap->lock);
}

void *kmalloc_aligned(size_t size, size_t align)
{
    if (!g_heap)
        return NULL;

    /* Over-allocate then align within the returned block. The padding
     * region wastes memory but keeps the data pointer 8-aligned-window
     * safe; callers needing strict alignment use kmalloc + manual align. */
    size_t padded = size + align + HDR_SIZE + HEAP_ALIGN;
    void *raw = kmalloc(padded);
    if (!raw)
        return NULL;

    uintptr_t addr = (uintptr_t)raw;
    uintptr_t aligned = ALIGN_UP(addr, align);
    return (void *)aligned;
}

void *krealloc(void *ptr, size_t size)
{
    if (!ptr)
        return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    struct heap_header *hdr = DATA_TO_HDR(ptr);
    size_t old_data = hdr->total_size - HDR_SIZE;

    if (old_data >= size)
        return ptr;

    void *newp = kmalloc(size);
    if (!newp)
        return NULL;
    memcpy(newp, ptr, old_data);
    kfree(ptr);
    return newp;
}

void kheap_dump(void)
{
    if (!g_heap) {
        printk("kheap: not initialized\n");
        return;
    }

    printk("kheap: arena=%p size=%u used=%u\n",
           g_heap, (unsigned)g_heap->total, (unsigned)g_heap->used);

    struct heap_header *hdr =
        (struct heap_header *)((u8 *)g_heap + sizeof(struct heap_arena));
    while ((u8 *)hdr + HDR_SIZE <= (u8 *)g_heap + g_heap->total &&
           hdr->magic == HEAP_HDR_MAGIC) {
        printk("kheap: block @%p size=%u %s\n", hdr,
               (unsigned)hdr->total_size,
               hdr->free ? "free" : "used");
        hdr = (struct heap_header *)((u8 *)hdr + hdr->total_size);
    }
}