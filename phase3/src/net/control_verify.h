/*
 * control_verify.h — control-IN inbound verification ORCHESTRATOR (Phase 6 6-5).
 *
 * Single entry point that runs a raw captured inbound frame through the full
 * control-IN gauntlet in a DELIBERATE, security-motivated order and hands the
 * caller a single verdict plus (only on ACCEPT) a copy-free view of the
 * authenticated query text. Host-pure: no seL4 / device / allocator dep, no
 * malloc, no libc beyond the dependency modules. Follows the km2b_miss.c /
 * wake.c / monitors.c precedent.
 *
 * ── PIPELINE ORDER (and WHY it is this order) ───────────────────────────────
 *
 *   1. PARSE   control_parse_frame() + control_parse_msg()   -> CV_DROP_PARSE
 *        Cheapest first. Bounds-checked, copy-free, fragment-reject. A malformed
 *        or fragmented datagram is discarded before any stateful or expensive
 *        work. Reads nothing past the captured length.
 *
 *   2. RATELIMIT  control_ratelimit_allow()                  -> CV_DROP_RATELIMIT
 *        Cheap token-bucket check, run BEFORE the expensive HMAC. This is the
 *        DoS shield: an attacker flooding well-formed-but-unauthenticated
 *        datagrams must not be able to make the box burn a SHA-256 pair per
 *        packet. Rate-limit gates the crypto, not the other way around.
 *
 *   3. AUTH   hmac_sha256_verify() over msg.mac_base[0..mac_len)  -> CV_DROP_AUTH
 *        Constant-time HMAC-SHA256 over the canonical span (36 + query_len,
 *        i.e. every datagram byte except the trailing 32-byte tag). No early
 *        exit — the tag compare is not a timing oracle.
 *
 *   4. REPLAY   control_replay_check()                       -> CV_DROP_REPLAY
 *        LAST, and this ORDER IS A HARD INVARIANT. control_replay_check()
 *        MUTATES persistent state (advances seq_floor, inserts into the nonce
 *        ring) and it does so ONLY on CR_ACCEPT. By running it strictly after a
 *        successful HMAC verify, only an AUTHENTICATED frame can ever move the
 *        floor or consume a nonce slot. If replay ran before auth, an
 *        unauthenticated attacker could walk seq_floor forward (or fill the
 *        nonce ring) and permanently lock out the real client — a trivial DoS.
 *        So: auth gates replay. Never reorder these two.
 *
 * ── OUTPUT CONTRACT ─────────────────────────────────────────────────────────
 *
 *   out->verdict          is ALWAYS set to the returned verdict.
 *   out->parse_status     carries the parse result (meaningful on CV_DROP_PARSE;
 *                         CTRL_OK once parsing succeeded).
 *   out->replay_verdict   carries the replay result (meaningful on CV_DROP_REPLAY).
 *   out->query / query_len are set ONLY on CV_ACCEPT and alias the input frame.
 *
 *   Every drop path is SILENT: query stays NULL / query_len 0, and no field
 *   beyond verdict + the relevant diagnostic is written. A caller can treat a
 *   non-CV_ACCEPT verdict as "there is no query" without inspecting anything.
 */
#ifndef JARVIS_CONTROL_VERIFY_H
#define JARVIS_CONTROL_VERIFY_H

#include <stdint.h>
#include <stddef.h>

#include "control_msg.h"
#include "control_parser.h"
#include "control_replay.h"
#include "control_ratelimit.h"

typedef enum {
    CV_ACCEPT = 0,
    CV_DROP_PARSE,
    CV_DROP_RATELIMIT,
    CV_DROP_AUTH,
    CV_DROP_REPLAY
} control_verdict_t;

typedef struct {
    control_verdict_t verdict;
    control_status_t  parse_status;   /* parse outcome; CTRL_OK once parsed     */
    cr_verdict_t      replay_verdict;  /* replay outcome; CR_ACCEPT until reached */
    const uint8_t    *query;           /* aliases the frame; set ONLY on ACCEPT   */
    uint16_t          query_len;       /* set ONLY on ACCEPT                      */
} control_result_t;

/*
 * Run one raw inbound frame through parse -> ratelimit -> auth -> replay.
 * Returns the verdict (also written to out->verdict). `out` MUST be non-NULL.
 * On CV_ACCEPT, out->query points into `frame` for out->query_len bytes.
 * Any drop leaves out->query NULL / out->query_len 0 (silent).
 */
control_verdict_t control_verify(const uint8_t *frame, size_t len,
                                 const uint8_t *key, size_t klen,
                                 control_replay_t *replay,
                                 control_ratelimit_t *rl,
                                 uint32_t now_ms,
                                 control_result_t *out);

#endif /* JARVIS_CONTROL_VERIFY_H */
