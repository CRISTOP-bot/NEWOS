#ifndef LIBK_BITMAP_H
#define LIBK_BITMAP_H

#include <kernel/types.h>

#define BITMAP_BITS_PER_WORD (sizeof(size_t) * 8)
#define BITMAP_WORDS(bits) \
    (((bits) + BITMAP_BITS_PER_WORD - 1) / BITMAP_BITS_PER_WORD)
#define BITMAP_BYTES(bits) (BITMAP_WORDS(bits) * sizeof(size_t))

static inline int bitmap_get(const size_t *map, size_t bit)
{
    return (map[bit / BITMAP_BITS_PER_WORD] >> (bit % BITMAP_BITS_PER_WORD)) & 1;
}

static inline void bitmap_set(size_t *map, size_t bit, int value)
{
    size_t word = bit / BITMAP_BITS_PER_WORD;
    size_t off  = bit % BITMAP_BITS_PER_WORD;
    if (value)
        map[word] |= (size_t)1 << off;
    else
        map[word] &= ~((size_t)1 << off);
}

static inline void bitmap_clear_all(size_t *map, size_t bits)
{
    size_t words = BITMAP_WORDS(bits);
    for (size_t i = 0; i < words; i++)
        map[i] = 0;
}

static inline void bitmap_set_all(size_t *map, size_t bits)
{
    size_t words = BITMAP_WORDS(bits);
    size_t partial = bits % BITMAP_BITS_PER_WORD;
    for (size_t i = 0; i < words; i++) {
        if ((i + 1 == words) && partial)
            map[i] = ((size_t)1 << partial) - 1;
        else
            map[i] = ~(size_t)0;
    }
}

/* Find the first free (zero) bit at or after `start`. Returns upper
 * bound `bits` if none exists. Runs in O(words) scan. */
size_t bitmap_find_first_bit(const size_t *map, size_t bits, size_t start);

#endif