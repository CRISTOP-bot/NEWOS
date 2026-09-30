#ifndef MM_SLAB_H
#define MM_SLAB_H

#include <core/core_types.h>

#define SLAB_FLAG_NONE  0x00
#define SLAB_FLAG_POISON 0x01
#define SLAB_FLAG_ZERO  0x02

typedef void (*slab_ctor_t)(void *obj, size_t size);
typedef void (*slab_dtor_t)(void *obj, size_t size);

typedef struct slab_cache slab_cache_t;

void slab_init(void);
slab_cache_t *slab_cache_create(const char *name, size_t obj_size, size_t align, uint32_t flags);
slab_cache_t *slab_cache_create_with_ctor(const char *name, size_t obj_size, size_t align, uint32_t flags,
                                          slab_ctor_t ctor, slab_dtor_t dtor);
void slab_cache_destroy(slab_cache_t *cache);
void *slab_cache_alloc(slab_cache_t *cache);
void slab_cache_free(slab_cache_t *cache, void *obj);

#endif
