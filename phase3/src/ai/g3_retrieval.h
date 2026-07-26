/*
 * JARVIS AI-OS — Phase 5 Goal #3 (Retrieval Before Inference), Milestone M0
 *
 * The retrieval SCORER + preamble ASSEMBLER as pure, host-testable C — NO seL4 /
 * device / model dependency. This is the consumer LOGIC only: sourcing the
 * candidate records (pool key→record / NVMe text) is M1, and injecting the
 * preamble into Process B's prompt is M2. Design: phase5/docs/
 * PHASE_5_GOAL3_RETRIEVAL.md (§2 scorer = exact-key + recency; §3 preamble).
 *
 * MVP scorer (§2): exact query_key match first, then a recency fallback — exact-key
 * and recency are the only signals that exist (decision-cache FNV-1a parity); the
 * caller passes the already-hashed query_key (no cache_hash here). All text fields
 * are NOT NUL-terminated — always copy by *_len, never strlen.
 */

#ifndef G3_RETRIEVAL_H
#define G3_RETRIEVAL_H

#include <stdint.h>

/* Tunables (§3 / D-d). */
#define G3_MAX_FACTS        2     /* preamble holds at most this many facts */
#define G3_Q_MAX            80    /* per-field query truncation (bytes) */
#define G3_R_MAX            120   /* WORKLOAD lane response truncation (bytes) -- UNCHANGED */
/* CONTROL-IN lane response truncation. PER-LANE, following M2's
 * PB_GEN_MAX_WORKLOAD / PB_GEN_MAX_CONTROL_IN precedent: g3_clean_answer_len is
 * SHARED and the workload retrieval lane is default-ON, so raising this globally
 * would change deployed workload generation -- the same blast radius f1fc84b had.
 *
 * 256 is PRINCIPLED, not tuned: it equals EPI_RESP_MAX, so it is the whole stored
 * answer and nothing beyond it exists to recall. (The _Static_assert pinning the
 * two together lives in test_g3_retrieval.c, which includes episodic_store.h --
 * this header stays deliberately decoupled from epi_record_t.)
 *
 * WHY: measured 2026-07-26 over the real control-IN store (62 usable records), a
 * 120-byte window yielded a MEDIAN preamble of 19 bytes -- and that string was
 * route_decline_text()'s canned "I don't track that.", the most recallable
 * content in the store precisely because it is short enough to fit with its full
 * stop. The window DECLINED every informative answer, each of whose first
 * complete sentence lands between 120 and 256 bytes. The rule selected FOR canned
 * refusals and AGAINST prose. */
#define G3_R_MAX_CONTROL_IN 256   /* control-IN lane -- the whole stored answer (== EPI_RESP_MAX) */
#define PREAMBLE_MAX_BYTES  512   /* total preamble cap (< ~600 so a prompt_ids[256] holds it) */

/* A retrieval candidate — decoupled from epi_record_t so this stays pure/host-testable.
 * query/resp are NOT NUL-terminated; always use query_len/resp_len. */
typedef struct {
    uint64_t    query_key;
    uint32_t    seq;          /* monotonic recency index (higher = newer) */
    uint16_t    action;
    uint8_t     outcome;
    const char *query;
    uint16_t    query_len;
    const char *resp;
    uint16_t    resp_len;
} g3_candidate_t;

/* Select up to `max` records into out[]: the EXACT query_key match first (newest seq wins,
 * deduped by key), then a RECENCY fallback (newest seq, deduped by key) for the remaining
 * slots. Deterministic + stable (input order breaks seq ties). Returns the count written
 * (0..max). out must hold `max` entries. */
int g3_select(const g3_candidate_t *cands, int n, uint64_t query_key, int max, g3_candidate_t *out);

/* Assemble a delimited preamble from the selected records into out[0..cap):
 *   "Known context:\n" then per fact "- " query(<=G3_Q_MAX) ": " resp(<=G3_R_MAX) "\n".
 * Per-field AND total (min(cap, PREAMBLE_MAX_BYTES)) truncation; copies text by *_len
 * (never strlen); always NUL-terminates; NEVER writes past cap. Returns the preamble length
 * (0 when n<=0 — the "no relevant memory => no preamble" path). */
int g3_build_preamble(const g3_candidate_t *sel, int n, char *out, int cap);

/* ---- G3/M6: injection-hygiene variants (fix the A/B P6 cross-topic echo) ---- */

/* Like g3_select but EXACT-KEY-ONLY: writes the NEWEST-seq candidate whose query_key == query_key
 * into out[0] and returns 1; returns 0 when there is NO exact-key match — NO recency fallback, so a
 * topically-distant newest fact can never be injected into an unrelated query. out must hold 1
 * entry. Pure/host-testable. */
int g3_select_exact_only(const g3_candidate_t *cands, int n, uint64_t query_key, g3_candidate_t *out);

/* Assemble a FENCED, ANSWER-ONLY preamble (NO embedded prior-question text — the anti-echo fix for
 * the G3/M6 A/B P6 leak) into out[0..cap):
 *   "Known context (your earlier answer to this question):\n" then per fact resp(<=G3_R_MAX) "\n".
 * Same discipline as g3_build_preamble: copy text by *_len (never strlen), total cap =
 * min(cap, PREAMBLE_MAX_BYTES)-1, always NUL-terminate, NEVER past cap. Returns the length
 * (0 when n<=0).
 *
 * `r_max` is the PER-LANE response window in bytes -- pass G3_R_MAX from the workload lane and
 * G3_R_MAX_CONTROL_IN from the control-IN lane. It is an explicit NUMBER rather than a lane enum
 * on purpose: the caller already knows which lane it is, and a number cannot be mapped wrongly. */
int g3_build_preamble_answer_only(const g3_candidate_t *sel, int n, char *out, int cap, int r_max);

/* Does `s` (length-carried, NOT NUL-terminated) contain the model's thinking-channel marker text
 * ("<|channel>" / "<channel|>" / "<|think|>")? Such a record is reasoning scratch, not an answer,
 * and must never be recalled: injecting it puts the literal marker back into the prompt and drives
 * the model to produce more thought. Pure/host-testable. */
int g3_text_has_thought_marker(const char *s, int len);

/* ---- G3/M2: prompt-injection token budget (pure, host-testable) ---- */

#define G3_PREAMBLE_TOK_CAP  160   /* hard cap on injected preamble tokens */
#define G3_QUERY_FLOOR_TOKS   48   /* tokens always kept free for the question */
#define G3_SUFFIX_TOKS         6   /* trailing template tokens (<turn|> \n <|turn> model \n <|think|>) */

/* Max preamble tokens that may be encoded into prompt_ids[] given the current fill `n_prompt`,
 * the array capacity `cap`, a `query_floor` (tokens reserved for the question), and `suffix`
 * (trailing template tokens). room = cap - n_prompt - suffix - query_floor, clamped to
 * [0, G3_PREAMBLE_TOK_CAP] — so the query is never starved and prompt_ids never overflows. */
static inline int g3_prompt_budget(int n_prompt, int cap, int query_floor, int suffix) {
    int room = cap - n_prompt - suffix - query_floor;
    if (room < 0) room = 0;
    if (room > G3_PREAMBLE_TOK_CAP) room = G3_PREAMBLE_TOK_CAP;
    return room;
}

/* ---- G3/M3: retrieval candidate usability filter (pure, host-testable) ---- */

/* A record is a usable retrieval candidate ONLY if it is a SUCCESSFUL inference with real response
 * text. Cache-action records carry canned/bytecode-ish "responses" that pollute the preamble and
 * push the model into <|channel> thought restatement (box-observed 2026-06-28); errored/empty
 * records add no context. Pure — host-tested. The caller passes the episodic action/outcome codes
 * (EPI_ACT_INFER / EPI_OUT_OK live in episodic_store.h) so this header stays decoupled from
 * epi_record_t. */
static inline int g3_candidate_usable(uint16_t action, uint8_t outcome, uint16_t resp_len,
                                      uint16_t infer_action, uint8_t ok_outcome) {
    return (action == infer_action) && (outcome == ok_outcome) && (resp_len > 0);
}

#endif /* G3_RETRIEVAL_H */
