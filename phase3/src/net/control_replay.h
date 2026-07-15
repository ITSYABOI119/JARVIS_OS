/*
 * control_replay.h — PA-side replay-defense state for the control-IN lane.
 *
 * Host-pure (no seL4 / device / allocator dependency), following the
 * phase3/src/ai/km2b_miss.c / wake.c / monitors.c precedent. NVMe persistence
 * of {seq_floor, boot_epoch} is a later milestone; this module models the pure
 * accept/reject decision plus a serialize/reload contract exercised on the host.
 *
 * Depends on control_msg.h for CONTROL_NONCE_LEN. That header is owned by a
 * sibling module and may not have landed yet; __has_include lets this compile
 * standalone with an honest fallback and automatically adopt the real value
 * once control_msg.h exists.
 */
#ifndef JARVIS_CONTROL_REPLAY_H
#define JARVIS_CONTROL_REPLAY_H

#include <stdint.h>

#if defined(__has_include)
#  if __has_include("control_msg.h")
#    include "control_msg.h"
#  endif
#endif

#ifndef CONTROL_NONCE_LEN
#define CONTROL_NONCE_LEN 16u
#endif

/* Size of the anti-replay nonce ring (finite sliding window — see honesty note
 * in control_replay.c). Overridable at build time. */
#ifndef CONTROL_NONCE_WINDOW
#define CONTROL_NONCE_WINDOW 128u
#endif

typedef struct {
    uint64_t seq_floor;                                   /* highest accepted seq; strict-greater required */
    uint32_t boot_epoch;                                  /* current boot identity; strict-equal required  */
    uint8_t  nonce_ring[CONTROL_NONCE_WINDOW][CONTROL_NONCE_LEN];
    uint16_t nonce_count;                                 /* live entries in the ring, saturates at WINDOW  */
    uint16_t nonce_head;                                  /* next insert slot (0..WINDOW-1)                 */
} control_replay_t;

typedef enum {
    CR_ACCEPT = 0,
    CR_REJECT_EPOCH,
    CR_REJECT_SEQ,
    CR_REJECT_NONCE
} cr_verdict_t;

/* Fresh state: floor=0, empty ring, given boot epoch. */
void control_replay_init(control_replay_t *st, uint32_t boot_epoch);

/*
 * Ordered, fail-closed replay check. Mutates state ONLY on CR_ACCEPT.
 *   1. epoch != boot_epoch          -> CR_REJECT_EPOCH
 *   2. seq   <= seq_floor           -> CR_REJECT_SEQ   (incl. first-ever floor=0 & seq=0; u64 no-wrap)
 *   3. nonce already present        -> CR_REJECT_NONCE
 *   otherwise ACCEPT: seq_floor=seq; ring[head]=nonce; head=(head+1)%N; count=min(count+1,N)
 */
cr_verdict_t control_replay_check(control_replay_t *st, uint64_t seq,
                                  uint32_t epoch, const uint8_t nonce[CONTROL_NONCE_LEN]);

#endif /* JARVIS_CONTROL_REPLAY_H */
