#include <libk/ringbuf.h>

void ringbuf_init(struct ringbuf *rb, void *data, size_t size)
{
    rb->data = (u8 *)data;
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
}

size_t ringbuf_free(const struct ringbuf *rb)
{
    return rb->size - ringbuf_len(rb) - 1;
}

size_t ringbuf_len(const struct ringbuf *rb)
{
    return (rb->head - rb->tail) & (rb->size - 1);
}

int ringbuf_push(struct ringbuf *rb, u8 value)
{
    if (ringbuf_free(rb) == 0)
        return -1;
    rb->data[rb->head] = value;
    rb->head = (rb->head + 1) & (rb->size - 1);
    return 0;
}

int ringbuf_pop(struct ringbuf *rb, u8 *value)
{
    if (ringbuf_len(rb) == 0)
        return -1;
    *value = rb->data[rb->tail];
    rb->tail = (rb->tail + 1) & (rb->size - 1);
    return 0;
}

size_t ringbuf_push_block(struct ringbuf *rb, const void *src, size_t n)
{
    const u8 *s = (const u8 *)src;
    size_t written = 0;
    while (written < n && ringbuf_push(rb, s[written]) == 0)
        written++;
    return written;
}

size_t ringbuf_pop_block(struct ringbuf *rb, void *dst, size_t n)
{
    u8 *d = (u8 *)dst;
    size_t read = 0;
    while (read < n && ringbuf_pop(rb, &d[read]) == 0)
        read++;
    return read;
}

void ringbuf_clear(struct ringbuf *rb)
{
    rb->head = 0;
    rb->tail = 0;
}