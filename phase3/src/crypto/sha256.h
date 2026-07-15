/*
 * sha256.h — SHA-256 (FIPS 180-4)
 *
 * Adapted from Brad Conte's public-domain crypto-algorithms
 * (https://github.com/B-Con/crypto-algorithms). Original WORD/BYTE/LONGLONG
 * types renamed to fixed-width <stdint.h> types. Public domain.
 *
 * Host-pure: no seL4/device/allocator dependency (km2b_miss.c precedent).
 * No malloc — all state is caller-provided/stack.
 */
#ifndef JARVIS_SHA256_H
#define JARVIS_SHA256_H

#include <stdint.h>
#include <stddef.h>

#define SHA256_BLOCK_SIZE 32  /* SHA-256 outputs a 32-byte digest */

typedef struct {
    uint8_t  data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} sha256_ctx;

void sha256_init(sha256_ctx *ctx);
void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx *ctx, uint8_t out[32]);

/* One-shot convenience wrapper. */
void sha256(const uint8_t *data, size_t len, uint8_t out[32]);

#endif /* JARVIS_SHA256_H */
