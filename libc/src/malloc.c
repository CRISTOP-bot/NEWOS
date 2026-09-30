#include "../include/stdlib.h"
#include "../include/string.h"

/* Bump + free-list heap over one static arena (256 KiB of .bss).
 *
 * There is no sbrk/mmap syscall yet, so the heap cannot grow: malloc
 * returns NULL when the arena is exhausted. Freed blocks coalesce with
 * neighbours; small churn (shell tools, demos) runs fine. Integer only.
 */

#define ARENA_SIZE (256u * 1024u)
#define ALIGN 16u
#define HDR_SIZE ((sizeof(struct hdr) + ALIGN - 1) & ~(size_t)(ALIGN - 1))
#define MIN_SPLIT (HDR_SIZE + ALIGN)

struct hdr {
    size_t size;            /* usable bytes after this header */
    int free;
    struct hdr *next;
};

static unsigned char arena[ARENA_SIZE];
static struct hdr *head;
static size_t bump;

static size_t align_up(size_t n)
{
    return (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
}

void *malloc(size_t n)
{
    struct hdr *h;
    size_t need;

    if (n == 0)
        n = 1;
    need = align_up(n);

    for (h = head; h; h = h->next) {
        if (h->free && h->size >= need) {
            if (h->size >= need + MIN_SPLIT) {
                struct hdr *rest =
                    (struct hdr *)((unsigned char *)(h + 1) + need);
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

    if (bump + HDR_SIZE + need > ARENA_SIZE)
        return NULL;
    h = (struct hdr *)(arena + bump);
    bump += HDR_SIZE + need;
    h->size = need;
    h->free = 0;
    h->next = head;
    head = h;
    return (void *)(h + 1);
}

void free(void *p)
{
    struct hdr *h, *cur;

    if (!p)
        return;
    h = (struct hdr *)p - 1;
    if ((unsigned char *)h < arena || (unsigned char *)h >= arena + ARENA_SIZE)
        return;             /* not ours: ignore, never corrupt */
    h->free = 1;

    /* Coalesce with the address-neighbour when it happens to be next in
     * the list (bump blocks are prepended, so this is best-effort). */
    for (cur = head; cur; cur = cur->next) {
        if (!cur->free)
            continue;
        {
            struct hdr *nxt = cur->next;
            unsigned char *end = (unsigned char *)(cur + 1) + cur->size;
            if (nxt && nxt->free && end == (unsigned char *)nxt) {
                cur->size += HDR_SIZE + nxt->size;
                cur->next = nxt->next;
            }
        }
    }
}

void *calloc(size_t n, size_t size)
{
    size_t total = n * size;
    void *p;
    if (n && total / n != size)
        return NULL;        /* overflow */
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
    if ((unsigned char *)h < arena || (unsigned char *)h >= arena + ARENA_SIZE)
        return NULL;
    if (h->size >= align_up(n))
        return p;
    np = malloc(n);
    if (!np)
        return NULL;
    keep = h->size < n ? h->size : n;
    memcpy(np, p, keep);
    free(p);
    return np;
}
