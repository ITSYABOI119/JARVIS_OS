/* control_ratelimit.h - token-bucket rate limiter for control-IN inbound.
 *
 * Host-pure (no seL4 dep), millisecond-time driven by the caller (the
 * km2b_miss.c / wake.c / monitors.c precedent). All arithmetic is uint32
 * wrap-safe so a wrapping millisecond clock is handled correctly.
 *
 * Model: a bucket holds up to CONTROL_RL_CAPACITY tokens, refilled at
 * CONTROL_RL_REFILL_PER_SEC tokens/second. Tokens are tracked in
 * milli-tokens (1 token = 1000 milli) so a partial second refills a
 * fractional token WITHOUT ever granting a whole one.
 */
#ifndef CONTROL_RATELIMIT_H
#define CONTROL_RATELIMIT_H

#include <stdint.h>

#ifndef CONTROL_RL_CAPACITY
#define CONTROL_RL_CAPACITY 8u
#endif

#ifndef CONTROL_RL_REFILL_PER_SEC
#define CONTROL_RL_REFILL_PER_SEC 1u
#endif

typedef struct {
    uint32_t tokens_milli; /* current tokens, in milli-tokens (1000 = 1 token) */
    uint32_t last_ms;      /* timestamp of the last refill/decision */
} control_ratelimit_t;

/* Initialize the bucket FULL (CAP tokens) at now_ms. */
void control_ratelimit_init(control_ratelimit_t *rl, uint32_t now_ms);

/* Try to consume one token at now_ms.
 * Returns 1 if a token was available (and consumed), 0 if dropped.
 */
int control_ratelimit_allow(control_ratelimit_t *rl, uint32_t now_ms);

#endif /* CONTROL_RATELIMIT_H */
