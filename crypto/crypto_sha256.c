#include <crypto/crypto.h>
#include <iru_string.h>

/* SHA-256, FIPS 180-4. Written from the specification's own pseudocode;
 * the message schedule is materialised per round instead of as a 64-word
 * array, which keeps the working set inside a single cache line. */

static const u32 sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c197e6u, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static inline u32 ror32(u32 x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

#define CH(x, y, z)   (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z)  (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define BS0(x)        (ror32(x, 2) ^ ror32(x, 13) ^ ror32(x, 22))
#define BS1(x)        (ror32(x, 6) ^ ror32(x, 11) ^ ror32(x, 25))
#define SS0(x)        (ror32(x, 7) ^ ror32(x, 18) ^ ((x) >> 3))
#define SS1(x)        (ror32(x, 17) ^ ror32(x, 19) ^ ((x) >> 10))

static void sha256_compress(u32 h[8], const u8 block[SHA256_BLOCK_SIZE])
{
    u32 w[16];
    u32 a, b, c, d, e, f, g, hh;
    unsigned i, t;

    for (i = 0; i < 16; i++)
        w[i] = ((u32)block[4 * i] << 24) | ((u32)block[4 * i + 1] << 16) |
               ((u32)block[4 * i + 2] << 8) | (u32)block[4 * i + 3];

    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; hh = h[7];

    /* The schedule is a 16-word sliding window: w[t] only ever needs
     * w[t-15], w[t-13], w[t-7] and w[t-2], so t % 16 is enough state. */
    for (t = 0; t < 64; t++) {
        u32 tmp;
        if (t >= 16)
            w[t % 16] += SS1(w[(t + 14) % 16]) + w[(t + 9) % 16] +
                         SS0(w[(t + 1) % 16]);
        tmp = hh + BS1(e) + CH(e, f, g) + sha256_k[t] + w[t % 16];
        hh = g; g = f; f = e; e = d + tmp;
        d = c; c = b; b = a; a = tmp + BS0(a) + MAJ(a, b, c);
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void sha256_init(struct sha256_ctx *ctx)
{
    ctx->h[0] = 0x6a09e667u; ctx->h[1] = 0xbb67ae85u;
    ctx->h[2] = 0x3c6ef372u; ctx->h[3] = 0xa54ff53au;
    ctx->h[4] = 0x510e527fu; ctx->h[5] = 0x9b05688cu;
    ctx->h[6] = 0x1f83d9abu; ctx->h[7] = 0x5be0cd19u;
    ctx->total = 0;
    ctx->buflen = 0;
}

void sha256_update(struct sha256_ctx *ctx, const void *data, u64 len)
{
    const u8 *src = (const u8 *)data;

    ctx->total += len;
    while (len) {
        u64 take = SHA256_BLOCK_SIZE - ctx->buflen;
        if (take > len)
            take = len;
        memcpy(ctx->buf + ctx->buflen, src, take);
        ctx->buflen += (u32)take;
        src += take;
        len -= take;
        if (ctx->buflen == SHA256_BLOCK_SIZE) {
            sha256_compress(ctx->h, ctx->buf);
            ctx->buflen = 0;
        }
    }
}

void sha256_final(struct sha256_ctx *ctx, u8 out[SHA256_DIGEST_SIZE])
{
    u64 bits = ctx->total * 8ull;
    u8 tail[SHA256_BLOCK_SIZE + 8];
    u32 zpad, i;

    /* 0x80, zeros, then the 64-bit big-endian bit count. The zero run is
     * chosen so the whole message ends on a block boundary, which can put
     * the length in a second block. */
    zpad = (ctx->buflen <= 55) ? (55 - ctx->buflen) : (119 - ctx->buflen);
    memset(tail, 0, sizeof(tail));
    tail[0] = 0x80;
    for (i = 0; i < 8; i++)
        tail[1 + zpad + i] = (u8)(bits >> (56 - 8 * i));
    sha256_update(ctx, tail, (u64)zpad + 9);

    for (i = 0; i < 8; i++) {
        out[4 * i]     = (u8)(ctx->h[i] >> 24);
        out[4 * i + 1] = (u8)(ctx->h[i] >> 16);
        out[4 * i + 2] = (u8)(ctx->h[i] >> 8);
        out[4 * i + 3] = (u8)(ctx->h[i]);
    }
    memset(ctx, 0, sizeof(*ctx));
}

void sha256(const void *data, u64 len, u8 out[SHA256_DIGEST_SIZE])
{
    struct sha256_ctx ctx;

    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}
