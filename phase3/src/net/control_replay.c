/*
 * control_replay.c — ordered, fail-closed replay defense (host-pure).
 *
 * Honesty note on the finite nonce window:
 *   The nonce ring holds at most CONTROL_NONCE_WINDOW distinct nonces. Once it
 *   is full, inserting a new nonce evicts the oldest. An evicted nonce is no
 *   longer remembered as "seen", so a *replayed* frame carrying that nonce would
 *   pass the nonce check — BUT the strictly-monotonic seq_floor is the primary
 *   anti-replay guarantee: any replay of an old frame also carries seq <=
 *   seq_floor and is rejected at step 2 (CR_REJECT_SEQ). The nonce ring only
 *   defends against duplicate nonces *within the window at the same-or-advancing
 *   seq* (e.g. a distinct-nonce requirement); it is deliberately finite and does
 *   not claim to remember every nonce ever seen.
 */
#include "control_replay.h"

#include <string.h>

void control_replay_init(control_replay_t *st, uint32_t boot_epoch)
{
    if (st == 0) {
        return;
    }
    memset(st, 0, sizeof(*st));
    st->seq_floor  = 0;
    st->boot_epoch = boot_epoch;
    st->nonce_count = 0;
    st->nonce_head  = 0;
}

static int nonce_in_ring(const control_replay_t *st, const uint8_t nonce[CONTROL_NONCE_LEN])
{
    uint16_t i;
    for (i = 0; i < st->nonce_count; i++) {
        if (memcmp(st->nonce_ring[i], nonce, CONTROL_NONCE_LEN) == 0) {
            return 1;
        }
    }
    return 0;
}

cr_verdict_t control_replay_check(control_replay_t *st, uint64_t seq,
                                  uint32_t epoch, const uint8_t nonce[CONTROL_NONCE_LEN])
{
    if (st == 0 || nonce == 0) {
        /* Fail closed: cannot verify -> reject. Treat missing nonce as a nonce
         * failure (the last ordered stage a caller could still recover from). */
        return CR_REJECT_NONCE;
    }

    /* (1) Boot epoch: strict equal. Prior-boot capture has epoch < current;
     * a forged future epoch cannot be signed. Both directions reject. */
    if (epoch != st->boot_epoch) {
        return CR_REJECT_EPOCH;
    }

    /* (2) Sequence floor: strictly greater than the highest accepted seq.
     * First-ever floor=0 with seq=0 rejects. u64 has no wrap in practice; a
     * wrapped value is <= floor and rejects. */
    if (seq <= st->seq_floor) {
        return CR_REJECT_SEQ;
    }

    /* (3) Nonce uniqueness within the finite window. */
    if (nonce_in_ring(st, nonce)) {
        return CR_REJECT_NONCE;
    }

    /* ACCEPT — the only path that mutates state. */
    st->seq_floor = seq;
    memcpy(st->nonce_ring[st->nonce_head], nonce, CONTROL_NONCE_LEN);
    st->nonce_head = (uint16_t)((st->nonce_head + 1u) % CONTROL_NONCE_WINDOW);
    if (st->nonce_count < CONTROL_NONCE_WINDOW) {
        st->nonce_count++;
    }
    return CR_ACCEPT;
}
