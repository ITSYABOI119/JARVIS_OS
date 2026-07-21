/*
 * control_mailbox.h — PA <-> jarvis-input shared-memory mailboxes (Phase 6 6-5/M2b-1).
 *
 * Model 2 (goal-doc §5, ratified 2026-07-15): Process A owns BAR0 + the I211 and
 * copies each raw captured inbound frame into the PA->input RAW mailbox; the
 * least-privileged jarvis-input process (SEC-014) runs ONLY control_parse_frame /
 * control_parse_msg + control_ratelimit and returns a CANDIDATE (the isolated JCTL
 * bytes + a status hint) in the input->PA CAND mailbox. PA then RE-PARSES the
 * candidate ITSELF and does the HMAC + replay (verify-in-PA) — it NEVER trusts a
 * forwarded offset/length. The input process holds ZERO NIC caps, no HMAC key, no
 * request/response ring, no model caps: a parser code-exec can only (a) write
 * garbage that PA HMAC-drops, or (b) stop forwarding (self-DoS). Under
 * KernelIOMMU=OFF, holding zero device-MMIO caps is what makes that containment
 * true (a BAR0 holder would be a bus-master DMA engine = root-equivalent).
 *
 * Each mailbox is a single 4 KB frame allocated by PA, mapped into PA at
 * CTRL_MBX_*_VADDR_PA and (a cap-dup of the same physical frame) into the input
 * process at CTRL_MBX_*_VADDR_IN. The `seq` field is a release/acquire handshake
 * (x86-TSO; the shared_context.c precedent): the WRITER bumps `seq` AFTER writing
 * the payload (RELEASE); the READER loads `seq` FIRST (ACQUIRE) and reads the
 * payload only when `seq` changed. PA keeps at most ONE frame in flight
 * (backpressure via g_ctrl_inflight), so the writer never overwrites a payload the
 * reader is still reading.
 *
 * The input process holds no timer caps and has no independent clock, so PA stamps
 * `fwd_ms` (jarvis_uptime_ms) into the raw mailbox alongside the payload and the
 * input process feeds THAT to control_ratelimit — the token bucket is millisecond-
 * driven, never a frame counter (a per-frame tick refills 1 milli-token per frame
 * against a 1000 milli-token cost: 8 frames, then the channel is dead).
 *
 * Host-pure header (no seL4 dep): the km2b_miss.h / control_msg.h precedent.
 */
#ifndef JARVIS_CONTROL_MAILBOX_H
#define JARVIS_CONTROL_MAILBOX_H

#include <stdint.h>

#include "control_msg.h"   /* CONTROL_MSG_MAX */

/* PA-side (rootserver) mailbox virtual addresses (map_frame_direct, fixed — the
 * SHMEM_VADDR_A=0x10000000 convention; adjacent, clear of the vspace allocator). */
#define CTRL_MBX_RAW_VADDR_PA    0x11000000UL   /* PA->input raw frame (PA view)    */
#define CTRL_MBX_CAND_VADDR_PA   0x11001000UL   /* input->PA candidate (PA view)    */
/* jarvis-input-side mailbox virtual addresses (the SAME two physical frames). */
#define CTRL_MBX_RAW_VADDR_IN    0x20000000UL   /* PA->input raw frame (input view) */
#define CTRL_MBX_CAND_VADDR_IN   0x20001000UL   /* input->PA candidate (input view) */

/* Largest raw frame PA copies into the RAW mailbox (>= the I211 2 KB RX buffer's
 * useful payload; a JCTL control frame is ~280 B). i211_nic_recv clamps to this. */
#define CTRL_MBX_MAX_FRAME  1536u

/* Candidate status hints (input->PA): why the input process rejected, so PA can
 * attribute a parse-vs-ratelimit drop without having to re-derive it. */
#define CTRL_CAND_OK             0u   /* parse + ratelimit passed; jctl[] is valid  */
#define CTRL_CAND_DROP_PARSE     1u   /* control_parse_frame/msg rejected           */
#define CTRL_CAND_DROP_RATELIMIT 2u   /* token bucket empty                         */

/* PA -> input: one raw captured Ethernet frame. */
typedef struct {
    volatile uint32_t seq;   /* PA bumps AFTER bytes (release); input loads first (acquire) */
    uint16_t len;            /* captured frame length (<= CTRL_MBX_MAX_FRAME)               */
    uint16_t _pad;
    uint32_t fwd_ms;         /* PA's jarvis_uptime_ms() at forward time; the input process's
                                rate-limiter clock (it has no independent clock)            */
    uint8_t  bytes[CTRL_MBX_MAX_FRAME];
} ctrl_raw_mbx_t;

/* input -> PA: the isolated JCTL bytes (Ethernet/IPv4/UDP envelope stripped) + a
 * status hint. jctl[] holds the UDP payload control_parse_frame isolated; PA
 * re-runs control_parse_msg over it (never trusting input's parse). */
typedef struct {
    volatile uint32_t seq;   /* input bumps AFTER payload (release); PA loads first (acquire) */
    uint16_t status;         /* CTRL_CAND_*                                                   */
    uint16_t jctl_len;       /* length of jctl[] (<= CONTROL_MSG_MAX)                          */
    uint8_t  jctl[CONTROL_MSG_MAX];
} ctrl_cand_mbx_t;

_Static_assert(sizeof(ctrl_raw_mbx_t)  <= 4096, "raw mailbox must fit one 4 KB page");
_Static_assert(sizeof(ctrl_cand_mbx_t) <= 4096, "candidate mailbox must fit one 4 KB page");

#endif /* JARVIS_CONTROL_MAILBOX_H */
