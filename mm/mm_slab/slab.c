#include <mm/mm_slab.h>
#include <mm/mm_heap.h>
#include <lib/kernel/iru_lock.h>
#include <lib/kernel/iru_string.h>
#include <core/core_printk.h>

#define SLAB_MIN_ALIGN 16
#define SLAB_MAX_CACHES 32

static char *strcpy_k(char *dst, const char *src) {
    char *orig = dst;
    while ((*dst++ = *src++));
    return orig;
}

struct slab_page {
    struct slab_page *next;
    u8 *data;
    u32 inuse;
    u32 total;
};

struct slab_cache {
    char name[32];
    size_t obj_size;
    size_t align;
    uint32_t flags;
    slab_ctor_t ctor;
    slab_dtor_t dtor;
    struct spinlock lock;
    struct slab_page *pages;
    size_t total_objs;
    size_t free_objs;
};

static slab_cache_t slab_caches[SLAB_MAX_CACHES];
static size_t slab_cache_count = 0;
static int slab_initialized = 0;

static size_t align_up(size_t val, size_t align) {
    if (align <= 1) return val;
    return (val + align - 1) & ~(align - 1);
}

static struct slab_page *alloc_slab_page(slab_cache_t *cache) {
    size_t page_size = 4096;
    size_t obj_size = align_up(cache->obj_size, SLAB_MIN_ALIGN);
    size_t objs_per_page = (page_size - sizeof(struct slab_page)) / obj_size;
    if (objs_per_page == 0) objs_per_page = 1;

    size_t alloc_size = sizeof(struct slab_page) + objs_per_page * obj_size;
    u8 *mem = (u8 *)kmalloc(alloc_size);
    if (!mem) return NULL;

    struct slab_page *sp = (struct slab_page *)mem;
    sp->next = NULL;
    sp->data = mem + sizeof(struct slab_page);
    sp->inuse = 0;
    sp->total = (u32)objs_per_page;

    return sp;
}

static void free_slab_page(slab_cache_t *cache, struct slab_page *sp) {
    kfree(sp);
}

void slab_init(void) {
    slab_cache_count = 0;
    slab_initialized = 1;
}

slab_cache_t *slab_cache_create(const char *name, size_t obj_size, size_t align, uint32_t flags) {
    return slab_cache_create_with_ctor(name, obj_size, align, flags, NULL, NULL);
}

slab_cache_t *slab_cache_create_with_ctor(const char *name, size_t obj_size, size_t align, uint32_t flags,
                                          slab_ctor_t ctor, slab_dtor_t dtor) {
    if (!slab_initialized) slab_init();
    if (slab_cache_count >= SLAB_MAX_CACHES) return NULL;

    slab_cache_t *cache = &slab_caches[slab_cache_count++];
    memset(cache, 0, sizeof(slab_cache_t));
    strcpy_k(cache->name, name);
    cache->obj_size = obj_size;
    cache->align = align ? align : SLAB_MIN_ALIGN;
    cache->flags = flags;
    cache->ctor = ctor;
    cache->dtor = dtor;
    spinlock_init(&cache->lock, name);
    cache->pages = NULL;
    cache->total_objs = 0;
    cache->free_objs = 0;

    return cache;
}

void slab_cache_destroy(slab_cache_t *cache) {
    spinlock_lock(&cache->lock);
    struct slab_page *sp = cache->pages;
    while (sp) {
        struct slab_page *next = sp->next;
        free_slab_page(cache, sp);
        sp = next;
    }
    cache->pages = NULL;
    cache->total_objs = 0;
    cache->free_objs = 0;
    spinlock_unlock(&cache->lock);
}

void *slab_cache_alloc(slab_cache_t *cache) {
    spinlock_lock(&cache->lock);

    struct slab_page *sp = cache->pages;
    if (!sp) {
        sp = alloc_slab_page(cache);
        if (!sp) {
            spinlock_unlock(&cache->lock);
            return NULL;
        }
        sp->next = cache->pages;
        cache->pages = sp;
    }

    size_t obj_size = align_up(cache->obj_size, SLAB_MIN_ALIGN);
    void *obj = sp->data + sp->inuse * obj_size;
    sp->inuse++;
    cache->total_objs++;
    cache->free_objs++;

    spinlock_unlock(&cache->lock);
    return obj;
}

void slab_cache_free(slab_cache_t *cache, void *obj) {
    if (!obj || !cache) return;
    spinlock_lock(&cache->lock);

    struct slab_page *sp = cache->pages;
    size_t obj_size = align_up(cache->obj_size, SLAB_MIN_ALIGN);
    while (sp) {
        if ((u8 *)obj >= sp->data && (u8 *)obj < sp->data + sp->total * obj_size) {
            sp->inuse--;
            cache->total_objs--;
            if (sp->inuse == 0 && sp != cache->pages) {
                struct slab_page *prev = cache->pages;
                if (prev->next == sp) prev->next = sp->next;
                free_slab_page(cache, sp);
            }
            spinlock_unlock(&cache->lock);
            return;
        }
        sp = sp->next;
    }

    spinlock_unlock(&cache->lock);
}
