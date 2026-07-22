/*
 * route.h — Phase 6 goal 6-6 / B/M0: the control-IN QUERY ROUTER.
 *
 * route_classify runs on a VALIDATED control-IN query (post-HMAC / post-replay /
 * post-SHIELD-ALLOW — query_shield/REFUSE is the exclusive UPSTREAM gate, never a
 * router class) and picks exactly ONE handler:
 *
 *   ROUTE_SYSFACTS (+ a sysfact_field_t) — the query asks for real box state the
 *                  box actually tracks (uptime / boot count / model / worker
 *                  count / coarse health); sysfacts_answer renders it from a
 *                  host-whitelisted struct of ground-truth fields.
 *   ROUTE_DECLINE  — a status-shaped query for a metric the box has NO source for
 *                  (cpu% / memory-used / disk-free / temperature / wall-clock) ->
 *                  a canned honest "I don't track that", NEVER a fabricated answer.
 *   ROUTE_INFER (=0) — everything else -> dispatch to Gemma; the SAFE DEFAULT.
 *
 * Host-pure: only <stdint.h>/<stddef.h>, no seL4 / device / allocator dep, no
 * malloc, no libc string calls — the query_shield.c / km2b_miss.c precedent.
 * Length-carried: every query is (q, len); the bytes are NEVER strlen'd. Crash/
 * UB-free for ANY bytes (0..255, embedded NUL, any length); pure + reentrant.
 *
 * ── THE HONESTY CEILING (do NOT overclaim; the query_shield.h precedent) ───────
 *
 *   route_classify is a COARSE, KEYWORD-based intent classifier over a DEFINED
 *   handler set. SYSTEM-FACTS answers only a host-whitelisted set of real
 *   box-state fields; a status query with no source DECLINES rather than letting
 *   the model fabricate; everything else is INFERENCE. It is NOT semantic — an
 *   embedding mechanism is a separate later arc (goal doc §8). The >=95% is
 *   measured on a KEYWORD-BLIND held-out suite, NOT production traffic.
 *
 * CONSERVATIVE rule (goal doc §7, load-bearing): DECLINE fires ONLY on the
 * explicit untracked-metric patterns; SYSFACTS only on high-confidence field
 * matches; ANY ambiguity -> ROUTE_INFER (fail toward the general model, never
 * toward a wrong fact). In particular the synthetic load-generator counters
 * ("how many queries/questions have you answered") are NOT a SYSFACTS field ->
 * ROUTE_INFER.
 *
 * WHITELIST RATIONALE (goal doc §4): sysfacts_answer reads ONLY human-meaningful
 * ground-truth fields. The load-generator counters (q_total/q_hits/q_errors —
 * the PRNG workload's, not the human's conversation) and all internal / security
 * state (key/floor/console gate booleans, JACT contents, shield_learn / monitor
 * internals, restart_count) are DELIBERATELY EXCLUDED — a fixed struct of allowed
 * fields, pinned by a host structural assertion. No-source metrics -> DECLINE,
 * never fabricated.
 */
#ifndef ROUTE_H
#define ROUTE_H

#include <stdint.h>
#include <stddef.h>

typedef enum { ROUTE_INFER = 0, ROUTE_SYSFACTS, ROUTE_DECLINE } route_class_t;

/* The host-whitelisted SYSTEM-FACTS fields (append-only; a snapshot may cite
 * these). SF_NONE on non-SYSFACTS; SF__COUNT is the exclusive upper bound. */
typedef enum {
    SF_NONE = 0,
    SF_UPTIME,    /* how long running / up / since boot (uptime_ms)            */
    SF_BOOT_ID,   /* how many boots / which boot / boot count (boot_id)        */
    SF_MODEL,     /* what model / which LLM (model_name)                       */
    SF_NODES,     /* how many workers / cores / threads (num_nodes)            */
    SF_HEALTH,    /* healthy? / any errors? / are you ok? (err==0 coarse bit)  */
    SF__COUNT
} sysfact_field_t;

typedef struct {
    route_class_t   cls;
    sysfact_field_t field;           /* SF_NONE unless cls == ROUTE_SYSFACTS   */
    int16_t         matched_pattern; /* rule id (>=0) that fired, or -1 (INFER) */
} route_result_t;

/*
 * Classify a length-carried validated query. `out` MAY be NULL (the verdict is
 * still returned). q == NULL or len == 0 -> ROUTE_INFER. Reads nothing past
 * q[len-1]; never strlen's q. Pure + reentrant (no globals mutated).
 */
route_class_t route_classify(const char *q, size_t len, route_result_t *out);

/*
 * The host-whitelisted ground-truth fields sysfacts_answer is ALLOWED to read.
 * Pinned by a structural assertion in test_route.c (goal doc §4d) so a future
 * field add is a conscious edit, never an honor-system read of arbitrary state.
 */
typedef struct {
    uint32_t uptime_ms;
    uint32_t boot_id;
    uint8_t  num_nodes;
    char     model_name[40];
    uint8_t  healthy;         /* err == 0 -> 1 (coarse bit), else 0 */
} sysfacts_t;

/*
 * Render the SYSTEM-FACTS answer for `field` from `facts` ONLY, into out[cap]
 * (bounded, always NUL-terminated). Returns the number of bytes written
 * (excluding the NUL); 0 for SF_NONE / any unmappable field / NULL args. Pure,
 * no malloc, no stdio (a self-contained bounded uint->decimal helper).
 */
int sysfacts_answer(sysfact_field_t field, const sysfacts_t *facts, char *out, size_t cap);

/* The canned honest decline text (goal doc §4: no-source -> DECLINE). */
const char *route_decline_text(void);

#endif /* ROUTE_H */
