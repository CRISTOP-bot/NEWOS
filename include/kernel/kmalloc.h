#ifndef MM_HEAP_KMALLOC_H
#define MM_HEAP_KMALLOC_H

#include <kernel/types.h>

void  *kmalloc(size_t size);
void  *kzalloc(size_t size);
void  *krealloc(void *ptr, size_t size);
void   kfree(void *ptr);
void  *kmalloc_aligned(size_t size, size_t align);
void   kheap_dump(void);

/* Early heap bootstrap. */
void   kheap_init(void *arena, size_t size);
void   kheap_init_early(void);

#endif