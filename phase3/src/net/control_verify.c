/*
 * control_verify.c — control-IN verification orchestrator (Phase 6 6-5).
 *
 * See control_verify.h for the contract and the (security-motivated) pipeline
 * ordering rationale. The one invariant that MUST NOT be relaxed: the replay
 * check runs LAST, strictly after a successful HMAC verify, so only an
 * authenticated frame can ever mutate the replay state (seq_floor / nonce ring).
 */

#include "control_verify.h"

#include "hmac_sha256.h"

control_verdict_t control_verify(const uint8_t *frame, size_t len,
                                 const uint8_t *key, size_t klen,
                                 control_replay_t *replay,
                                 control_ratelimit_t *rl,
                                 uint32_t now_ms,
                                 control_result_t *out)
{
    /* Clean, SILENT default: no query, benign diagnostics. Every early return
     * below only overrides out->verdict and (at most) one diagnostic field. */
    out->verdict        = CV_DROP_PARSE;
    out->parse_status   = CTRL_TOO_SHORT;
    out->replay_verdict = CR_ACCEPT;
    out->query          = NULL;
    out->query_len      = 0;

    if (!frame || !key || !replay || !rl) {
        out->verdict = CV_DROP_PARSE;
        return out->verdict;
    }

    /* 1) PARSE — envelope then message. Cheapest, bounds-checked, copy-free. */
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    control_status_t st = control_parse_frame(frame, len, &payload, &payload_len);
    if (st != CTRL_OK) {
        out->parse_status = st;
        out->verdict = CV_DROP_PARSE;
        return out->verdict;
    }

    control_msg_t msg;
    st = control_parse_msg(payload, payload_len, &msg);
    if (st != CTRL_OK) {
        out->parse_status = st;
        out->verdict = CV_DROP_PARSE;
        return out->verdict;
    }
    out->parse_status = CTRL_OK;

    /* 2) RATELIMIT — cheap, BEFORE the expensive HMAC (DoS shield). */
    if (!control_ratelimit_allow(rl, now_ms)) {
        out->verdict = CV_DROP_RATELIMIT;
        return out->verdict;
    }

    /* 3) AUTH — constant-time HMAC over the canonical span (36 + query_len). */
    if (!hmac_sha256_verify(key, klen, msg.mac_base, msg.mac_len, msg.tag)) {
        out->verdict = CV_DROP_AUTH;
        return out->verdict;
    }

    /* 4) REPLAY — LAST. Mutates state ONLY on CR_ACCEPT, and we only reach it
     * on an HMAC-valid frame, so an unauthenticated attacker can never advance
     * seq_floor or fill the nonce ring. */
    cr_verdict_t rv = control_replay_check(replay, msg.seq, msg.boot_epoch, msg.nonce);
    out->replay_verdict = rv;
    if (rv != CR_ACCEPT) {
        out->verdict = CV_DROP_REPLAY;
        return out->verdict;
    }

    /* ACCEPT — expose the authenticated query (copy-free view into `frame`). */
    out->query     = msg.query;
    out->query_len = msg.query_len;
    out->verdict   = CV_ACCEPT;
    return out->verdict;
}
