/*
 * hmac_sha256.h — HMAC-SHA-256 (RFC 2104) + constant-time verify
 *
 * Host-pure: no seL4/device/allocator dependency (km2b_miss.c precedent).
 * No malloc — all state is fixed-size / stack. Depends only on sha256.c.
 *
 * Phase 6 goal 6-5 (control-IN) security core: authenticates inbound
 * control messages. The verify path is CONSTANT-TIME (no data-dependent
 * control flow in the compare loop) so a tag compare is not a timing oracle.
 */
#ifndef JARVIS_HMAC_SHA256_H
#define JARVIS_HMAC_SHA256_H

#include <stdint.h>
#include <stddef.h>

/* Compute HMAC-SHA-256 of msg under key -> out[32]. */
void hmac_sha256(const uint8_t *key, size_t klen,
                 const uint8_t *msg, size_t mlen,
                 uint8_t out[32]);

/*
 * Verify tag == HMAC-SHA-256(key, msg) in CONSTANT TIME.
 * Returns 1 on match, 0 on mismatch. No early exit — every tag byte is read.
 */
int hmac_sha256_verify(const uint8_t *key, size_t klen,
                       const uint8_t *msg, size_t mlen,
                       const uint8_t tag[32]);

#endif /* JARVIS_HMAC_SHA256_H */
