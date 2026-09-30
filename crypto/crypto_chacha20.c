#include <crypto/crypto.h>

/* ChaCha20 as defined by RFC 8439: 20 rounds, the original DJB constant
 * words, a 32-bit block counter and a 96-bit nonce. Byte-oriented load and
 * store so the code never forms an unaligned 32-bit access. */

static inline u32 rotl32(u32 x, unsigned n)
{
    return (x << n) | (x >> (32 - n));
}

static inline u32 ld32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static inline void st32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

#define QR(a, b, c, d)                                     \
    do {                                                   \
        x[a] += x[b]; x[d] = rotl32(x[d] ^ x[a], 16);      \
        x[c] += x[d]; x[b] = rotl32(x[b] ^ x[c], 12);      \
        x[a] += x[b]; x[d] = rotl32(x[d] ^ x[a], 8);       \
        x[c] += x[d]; x[b] = rotl32(x[b] ^ x[c], 7);       \
    } while (0)

static void chacha20_block(u32 counter, const u8 key[CHACHA20_KEY_SIZE],
                           const u8 nonce[CHACHA20_NONCE_SIZE],
                           u8 out[64])
{
    u32 x[16];
    u32 work[16];
    int i;

    work[0] = 0x61707865u; work[1] = 0x33206467u;
    work[2] = 0x79622d32u; work[3] = 0x6b206e61u;
    for (i = 0; i < 8; i++)
        work[4 + i] = ld32(key + 4 * i);
    work[12] = counter;
    for (i = 0; i < 3; i++)
        work[13 + i] = ld32(nonce + 4 * i);

    for (i = 0; i < 16; i++)
        x[i] = work[i];

    for (i = 0; i < 10; i++) {
        QR(0, 4, 8, 12);
        QR(1, 5, 9, 13);
        QR(2, 6, 10, 14);
        QR(3, 7, 11, 15);
        QR(0, 5, 10, 15);
        QR(1, 6, 11, 12);
        QR(2, 7, 8, 13);
        QR(3, 4, 9, 14);
    }

    for (i = 0; i < 16; i++)
        st32(out + 4 * i, x[i] + work[i]);
}

void chacha20_keystream(u8 *dst, u64 len,
                        const u8 key[CHACHA20_KEY_SIZE],
                        const u8 nonce[CHACHA20_NONCE_SIZE], u32 counter)
{
    u8 block[64];
    u64 done = 0;

    while (done < len) {
        u64 take = len - done;
        unsigned i;

        if (take > 64)
            take = 64;
        chacha20_block(counter + (u32)(done / 64), key, nonce, block);
        for (i = 0; i < (unsigned)take; i++)
            dst[done + i] = block[i];
        done += take;
    }
}

void chacha20_xor(u8 *dst, const u8 *src, u64 len,
                  const u8 key[CHACHA20_KEY_SIZE],
                  const u8 nonce[CHACHA20_NONCE_SIZE], u32 counter)
{
    u8 block[64];
    u64 done = 0;

    while (done < len) {
        u64 take = len - done;
        unsigned i;

        if (take > 64)
            take = 64;
        chacha20_block(counter + (u32)(done / 64), key, nonce, block);
        for (i = 0; i < (unsigned)take; i++)
            dst[done + i] = src[done + i] ^ block[i];
        done += take;
    }
}
