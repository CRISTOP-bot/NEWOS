#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/unistd.h"
#include "../include/errno.h"

/* Free-list allocator backed by the process break. The kernel already
 * grows user mappings through SYS_BRK; using it removes the old 256 KiB
 * static ceiling and avoids reserving a large BSS arena in every process. */

#define ALIGN 16u
#define HDR_SIZE ((sizeof(struct hdr) + ALIGN - 1) & ~(size_t)(ALIGN - 1))
#define MIN_SPLIT (HDR_SIZE + ALIGN)

struct hdr {
    size_t size;
    int free;
    struct hdr *next;
};

static struct hdr *head;
static unsigned char *heap_start;
static unsigned char *heap_end;

static size_t align_up(size_t n)
{
    return (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
}

void *malloc(size_t n)
{
    struct hdr *h;
    size_t need, total;
    void *mem;

    if (n == 0)
        n = 1;
    if (n > (~(size_t)0) - (ALIGN - 1)) {
        errno = ENOMEM;
        return NULL;
    }
    need = align_up(n);
    if (need > (~(size_t)0) - HDR_SIZE ||
        need > ((~(size_t)0) >> 1) - HDR_SIZE) {
        errno = ENOMEM;
        return NULL;
    }
    total = HDR_SIZE + need;

    for (h = head; h; h = h->next) {
        if (h->free && h->size >= need) {
            if (h->size - need >= MIN_SPLIT) {
                struct hdr *rest = (struct hdr *)((unsigned char *)(h + 1) + need);
                rest->size = h->size - need - HDR_SIZE;
                rest->free = 1;
                rest->next = h->next;
                h->size = need;
                h->next = rest;
            }
            h->free = 0;
            return (void *)(h + 1);
        }
    }

    mem = sbrk((long)total);
    if (mem == (void *)-1)
        return NULL;
    if (!heap_start)
        heap_start = (unsigned char *)mem;
    heap_end = (unsigned char *)mem + total;
    h = (struct hdr *)mem;
    h->size = need;
    h->free = 0;
    h->next = head;
    head = h;
    return (void *)(h + 1);
}

void free(void *p)
{
    struct hdr *h;

    if (!p)
        return;
    h = (struct hdr *)p - 1;
    if (!heap_start || (unsigned char *)h < heap_start ||
        (unsigned char *)h >= heap_end)
        return;
    h->free = 1;

    /* Allocations are prepended, so adjacent free blocks can appear in
     * either address order in the list. Merge until no neighbor remains. */
    for (;;) {
        int merged = 0;
        for (struct hdr *a = head; a && !merged; a = a->next) {
            if (!a->free)
                continue;
            for (struct hdr *b = head; b; b = b->next) {
                if (a == b || !b->free)
                    continue;
                if ((unsigned char *)(a + 1) + a->size ==
                    (unsigned char *)b) {
                    struct hdr **link = &head;
                    a->size += HDR_SIZE + b->size;
                    while (*link != b)
                        link = &(*link)->next;
                    *link = b->next;
                    merged = 1;
                    break;
                }
                if ((unsigned char *)(b + 1) + b->size ==
                    (unsigned char *)a) {
                    struct hdr **link = &head;
                    b->size += HDR_SIZE + a->size;
                    while (*link != a)
                        link = &(*link)->next;
                    *link = a->next;
                    merged = 1;
                    break;
                }
            }
        }
        if (!merged)
            break;
    }
}

void *calloc(size_t n, size_t size)
{
    size_t total;
    void *p;
    if (n && size > (~(size_t)0) / n) {
        errno = ENOMEM;
        return NULL;
    }
    total = n * size;
    p = malloc(total);
    if (p)
        memset(p, 0, total);
    return p;
}

void *realloc(void *p, size_t n)
{
    struct hdr *h;
    void *np;
    size_t keep;

    if (!p)
        return malloc(n);
    if (n == 0) {
        free(p);
        return NULL;
    }
    h = (struct hdr *)p - 1;
    if (!heap_start || (unsigned char *)h < heap_start ||
        (unsigned char *)h >= heap_end)
        return NULL;
    if (n <= h->size)
        return p;
    np = malloc(n);
    if (!np)
        return NULL;
    keep = h->size < n ? h->size : n;
    memcpy(np, p, keep);
    free(p);
    return np;
}
