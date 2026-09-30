#ifndef KERNEL_CRYPTO_H
#define KERNEL_CRYPTO_H

#include <core/core_types.h>

/* Kernel cryptographic layer.
 *
 * Two families, each with an in-tree consumer:
 *   - SHA-256 (FIPS 180-4) backs package/image integrity and SYS_HASH.
 *   - ChaCha20 (RFC 8439) is the stream core of the kernel CSPRNG and
 *     therefore backs getrandom(2), /dev/random and the AT_RANDOM slot.
 *
 * Everything here is plain C over the kernel integer aliases: the kernel is
 * built with -mno-sse -mno-mmx -mgeneral-regs-only, so no SIMD intrinsics
 * are available and none are used. Buffers are caller-owned; nothing in this
 * layer allocates.
 */

#define SHA256_BLOCK_SIZE   64
#define SHA256_DIGEST_SIZE  32

#define CHACHA20_KEY_SIZE   32
#define CHACHA20_NONCE_SIZE 12

struct sha256_ctx {
    u32 h[8];
    u64 total;
    u32 buflen;
    u8 buf[SHA256_BLOCK_SIZE];
};

void sha256_init(struct sha256_ctx *ctx);
void sha256_update(struct sha256_ctx *ctx, const void *data, u64 len);
void sha256_final(struct sha256_ctx *ctx, u8 out[SHA256_DIGEST_SIZE]);

/* One-shot form: the output is always 32 bytes. */
void sha256(const void *data, u64 len, u8 out[SHA256_DIGEST_SIZE]);

/* ChaCha20 block function: `counter` is the 32-bit block index and the
 * keystream for exactly `len` bytes starting at that block offset is XORed
 * in place. `len` may cross block boundaries freely. */
void chacha20_xor(u8 *dst, const u8 *src, u64 len,
                  const u8 key[CHACHA20_KEY_SIZE],
                  const u8 nonce[CHACHA20_NONCE_SIZE], u32 counter);

/* Generate `len` bytes of key stream (i.e. chacha20_xor against zeros). */
void chacha20_keystream(u8 *dst, u64 len,
                        const u8 key[CHACHA20_KEY_SIZE],
                        const u8 nonce[CHACHA20_NONCE_SIZE], u32 counter);

/* Deterministic random-bit generator: a ChaCha20 keystream whose state is
 * rekeyed from every request and from the hardware source at boot. Not
 * fork-safe across CPUs beyond that, and has no accounting of an entropy
 * estimate -- /dev/random blocks the same way /dev/urandom does. */
void crypto_init(void);

/* Kernel-internal fill. Never fails; `len` bytes are always produced. */
void crypto_getrandom(void *buf, u64 len);

/* A 64-bit word from the same stream, for in-kernel jitter/nonce use. */
u64 crypto_getrandom_u64(void);

/* Self-test against the published vectors. Returns 0 when every case
 * matches, -1 otherwise. */
int crypto_selftest(void);

#endif
