#ifndef LIBK_RINGBUF_H
#define LIBK_RINGBUF_H

#include <kernel/types.h>

struct ringbuf {
    u8 *data;
    size_t size;      /* power of two */
    size_t head;      /* write index */
    size_t tail;      /* read index */
};

void     ringbuf_init(struct ringbuf *rb, void *data, size_t size);
size_t   ringbuf_free(const struct ringbuf *rb);
size_t   ringbuf_len(const struct ringbuf *rb);
int      ringbuf_push(struct ringbuf *rb, u8 value);
int      ringbuf_pop(struct ringbuf *rb, u8 *value);
size_t   ringbuf_push_block(struct ringbuf *rb, const void *src, size_t n);
size_t   ringbuf_pop_block(struct ringbuf *rb, void *dst, size_t n);
void     ringbuf_clear(struct ringbuf *rb);

#endif