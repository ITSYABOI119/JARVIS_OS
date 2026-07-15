/* control_ratelimit.c - see control_ratelimit.h */
#include "control_ratelimit.h"

/* CAP in milli-tokens; refill rate in milli-tokens per millisecond-second.
 * refill_milli_per_ms is fractional, so we accumulate as elapsed_ms * refill
 * (milli-tokens per second) / 1000. Doing it as (elapsed * refill) with the
 * /1000 folded in keeps partial seconds fractional in the milli domain:
 *   refill milli-tokens over `elapsed_ms` ms = elapsed_ms * REFILL_PER_SEC.
 * (1 token/sec -> 1000 milli/sec -> 1 milli per ms == elapsed_ms * 1.)
 */
#define CONTROL_RL_CAP_MILLI  (CONTROL_RL_CAPACITY * 1000u)

void control_ratelimit_init(control_ratelimit_t *rl, uint32_t now_ms)
{
    if (rl == 0) {
        return;
    }
    rl->tokens_milli = CONTROL_RL_CAP_MILLI;
    rl->last_ms = now_ms;
}

int control_ratelimit_allow(control_ratelimit_t *rl, uint32_t now_ms)
{
    uint32_t elapsed;
    uint32_t add;
    uint32_t tok;

    if (rl == 0) {
        return 0;
    }

    /* uint32 wrap-safe elapsed (the wake.c precedent): modular subtraction. */
    elapsed = now_ms - rl->last_ms;

    /* milli-tokens gained = elapsed_ms * REFILL_PER_SEC (partial second stays
     * fractional in the milli domain -> never grants a whole token early). */
    add = elapsed * CONTROL_RL_REFILL_PER_SEC;

    tok = rl->tokens_milli;
    /* Saturating add, then clamp to capacity. */
    if (add > CONTROL_RL_CAP_MILLI - tok) {
        /* would overflow the cap headroom (also covers uint32 overflow of the
         * raw sum, since tok <= CAP_MILLI) -> clamp to full */
        tok = CONTROL_RL_CAP_MILLI;
    } else {
        tok += add;
    }

    rl->last_ms = now_ms;

    if (tok >= 1000u) {
        tok -= 1000u;
        rl->tokens_milli = tok;
        return 1;
    }

    rl->tokens_milli = tok;
    return 0;
}
