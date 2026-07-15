/*
 * hmac_sha256.c — HMAC-SHA-256 (RFC 2104) + constant-time verify
 *
 * out = H( (K0 ^ opad) || H( (K0 ^ ipad) || msg ) )
 * where K0 is the key zero-padded to the 64-byte block size (or SHA-256 of
 * the key first when klen > 64). Fixed stack buffers, no malloc.
 */
#include "hmac_sha256.h"
#include "sha256.h"

#include <string.h>

#define HMAC_BLOCK 64  /* SHA-256 block size in bytes */

void hmac_sha256(const uint8_t *key, size_t klen,
                 const uint8_t *msg, size_t mlen,
                 uint8_t out[32])
{
    uint8_t k0[HMAC_BLOCK];
    uint8_t ipad[HMAC_BLOCK];
    uint8_t opad[HMAC_BLOCK];
    uint8_t inner[32];
    sha256_ctx ctx;
    int i;

    /* K0: keys longer than the block are hashed, then zero-padded. */
    memset(k0, 0, sizeof(k0));
    if (klen > HMAC_BLOCK) {
        sha256(key, klen, k0);          /* first 32 bytes = H(key); rest stay 0 */
    } else {
        memcpy(k0, key, klen);          /* remaining bytes stay 0 */
    }

    for (i = 0; i < HMAC_BLOCK; i++) {
        ipad[i] = (uint8_t)(k0[i] ^ 0x36);
        opad[i] = (uint8_t)(k0[i] ^ 0x5c);
    }

    /* inner = H( ipad || msg ) */
    sha256_init(&ctx);
    sha256_update(&ctx, ipad, HMAC_BLOCK);
    if (mlen != 0) {
        sha256_update(&ctx, msg, mlen);
    }
    sha256_final(&ctx, inner);

    /* out = H( opad || inner ) */
    sha256_init(&ctx);
    sha256_update(&ctx, opad, HMAC_BLOCK);
    sha256_update(&ctx, inner, sizeof(inner));
    sha256_final(&ctx, out);
}

int hmac_sha256_verify(const uint8_t *key, size_t klen,
                       const uint8_t *msg, size_t mlen,
                       const uint8_t tag[32])
{
    uint8_t calc[32];
    volatile uint8_t diff = 0;
    int i;

    hmac_sha256(key, klen, msg, mlen, calc);

    /* no data-dependent control flow in the compare loop */
    for (i = 0; i < 32; i++) {
        diff |= (uint8_t)(calc[i] ^ tag[i]);
    }
    return diff == 0;
}
