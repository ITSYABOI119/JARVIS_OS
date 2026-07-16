/*
 * query_shield.h — Phase 6 goal 6-5 / M3-1: the control-IN QUERY SHIELD.
 *
 * A control-IN frame that survives auth + replay (M1/M2b) yields a VALIDATED,
 * length-carried QUERY (<= CONTROL_QUERY_MAX = 172 B). This module decides
 * ALLOW vs REFUSE for a DEFINED, TRACTABLE set of hostile query classes before
 * PA would route it to inference (the box route + the induced-BLOCK proof +
 * response are M3-2). Host-pure: no seL4 / device / allocator dep, no malloc,
 * no libc beyond <stdint.h>/<stddef.h> — the km2b_miss.c / control_parser.c
 * precedent. Crash/UB-free for ANY attacker bytes (0..255, embedded NUL, any
 * length); the query string is length-carried and NEVER strlen'd.
 *
 * ── THE HONESTY CEILING (O-Q4 — the crux; do NOT overclaim) ──────────────────
 *
 * This is a COARSE ABUSE-REFUSER for a small set of DEFINED query classes — it
 * is NOT a prompt-injection detector. A local Gemma 4 E2B + no injection
 * detector cannot reliably catch subtle / novel / obfuscated prompt injection
 * (an unsolved problem). The REAL protection against injection is STRUCTURAL and
 * already built: K-b (inbound text can never mint an action), tagged-untrusted
 * (inbound queries are excluded from cache-growth promotion + retrieval-sourcing
 * -> no store contamination), and answer-to-console-address-only (no exfiltration
 * channel).
 *
 *   HONEST CLAIM (the ONLY one this module / its docs / its telemetry may make):
 *     "Refuses a defined abuse class (secret-extraction, bulk-exfil, canned
 *      jailbreak, config-disclosure) and audits it; general prompt injection is
 *      contained STRUCTURALLY — no-action, no-store, no-exfil — not detected."
 *
 *   BANNED overclaims anywhere: "blocks injection", "blocks jailbreaks", "detects
 *   malicious intent", "prevents prompt injection", "100% blocked", "injection-proof".
 *
 * Design: phase6/docs/PHASE_6_GOAL_6-5_CONTROL_IN.md (M3-1). This is NOT the
 * action keyword blocklist (shield_action.c) — that false-positives on benign
 * query text ("how do I delete a file?" -> `delete`) and has zero query-threat
 * coverage — and NOT shield.c's model-assisted shield_check (not host-pure, not
 * linked live — SEC-039).
 */
#ifndef QUERY_SHIELD_H
#define QUERY_SHIELD_H

#include <stdint.h>
#include <stddef.h>

typedef enum { QS_ALLOW = 0, QS_REFUSE = 1 } query_verdict_t;

/* The defined, tractable abuse classes. Append-only (a snapshot may cite these). */
typedef enum {
    QR_NONE = 0,
    QR_KEY_EXTRACTION,    /* (a) ask the system to EMIT its auth material (hmac/signing key, auth secret, JKEY) */
    QR_EXFIL,             /* (b) bulk memory/store harvest (a quantifier + a store noun)                       */
    QR_JAILBREAK,         /* (c) CANNED instruction-override / DAN / system-prompt-leak (known-string backstop) */
    QR_CONFIG_DISCLOSE,   /* (d) enumerate the security surface (allowlist / blocklist / trust levels / ids)    */
    QR__COUNT
} query_reason_t;

typedef struct {
    query_verdict_t verdict;         /* QS_ALLOW / QS_REFUSE                                  */
    query_reason_t  reason;          /* QR_NONE on ALLOW; the class on REFUSE                 */
    int16_t         matched_pattern; /* index into the static pattern table, or -1 on ALLOW  */
} query_shield_result_t;

/*
 * Assess a length-carried untrusted query. `out` MAY be NULL (verdict still
 * returned). len == 0 or q == NULL -> QS_ALLOW. Reads nothing past q[len-1];
 * never strlen's q. Pure + reentrant (no globals mutated).
 */
query_verdict_t query_shield_assess(const char *q, size_t len, query_shield_result_t *out);

/*
 * The fixed reason-class label a REFUSE audit records (M3-2 audits the label
 * ONLY, NEVER the raw query — so a hostile query cannot smuggle a blocklist
 * keyword that would make the audit's own shield_assess BLOCK/mangle the record).
 * Every label is verified keyword-clean against the canonical action blocklist.
 */
const char *query_shield_reason_label(query_reason_t r);

#endif /* QUERY_SHIELD_H */
