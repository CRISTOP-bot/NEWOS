#include <iru_bitmap.h>

size_t bitmap_find_first_bit(const size_t *map, size_t bits, size_t start)
{
    if (start >= bits)
        return bits;

    size_t i = start / BITMAP_BITS_PER_WORD;
    size_t off = start % BITMAP_BITS_PER_WORD;

    /* Scan the first (possibly partial) word. */
    size_t word = map[i];
    size_t mask = ~(size_t)0 << off;
    word |= ~mask;                    /* treat bits below `off` as set */
    for (size_t b = off; b < BITMAP_BITS_PER_WORD; b++) {
        if (!((word >> b) & 1)) {
            size_t bit = i * BITMAP_BITS_PER_WORD + b;
            return (bit < bits) ? bit : bits;
        }
    }

    for (i++; i < BITMAP_WORDS(bits); i++) {
        if (map[i] != ~(size_t)0) {
            for (size_t b = 0; b < BITMAP_BITS_PER_WORD; b++) {
                if (!((map[i] >> b) & 1)) {
                    size_t bit = i * BITMAP_BITS_PER_WORD + b;
                    return (bit < bits) ? bit : bits;
                }
            }
        }
    }

    return bits;
}