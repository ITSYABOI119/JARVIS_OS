/*
 * JARVIS AI-OS — Phase 5 Goal #4: Semantic Distill (deterministic — #7's core)
 *
 * The pure "compact episodic -> semantic" rule (this IS consolidation #7's
 * distill core, landed under #4): group episodic records by query_key, count
 * SUPPORT over usable records only (the proven #6/G3 filter — a real inference
 * answer, never a canned cache echo or a failure), select the NEWEST usable
 * answer, and emit one semantic_fact_t per key whose support >= min_support.
 *
 * DETERMINISTIC, frequency/consistency-based: NO LLM, NO embeddings (Phase 7 —
 * PHASE_5_PLAN.md §7.2/§7.6). HONEST CEILING: this extracts OBSERVABLE PATTERNS
 * (recurring Q&A) — it never "knows preferences" or "understands".
 *
 * Pure logic: no I/O, no device, no model. Text is carried by pointer + *_len,
 * never strlen'd (episodic resp[] is not NUL-required). Host-testable.
 */

#ifndef SEMANTIC_DISTILL_H
#define SEMANTIC_DISTILL_H

#include "episodic_store.h"
#include "semantic_store.h"

/* Default support threshold (D-b): a subject must have been answered this many
 * times (usable records) before it distills into a durable fact. */
#define SEM_MIN_SUPPORT  3

/* 1 if the record can source a fact: a REAL inference answer (action INFER,
 * outcome OK, non-empty resp) — mirrors g3_candidate_usable / the #6 promotion
 * filter, so cache echoes and failures never become "facts". Pure. */
int sd_record_usable(const epi_record_t *rec);

/* Group recs[0..n) by query_key, count support over USABLE records, select the
 * newest usable answer; emit a fact per key with support >= min_support into
 * out[0..max) (dedup-by-key; newest-wins; text copied by resp_len, clamped to
 * SEM_FACT_TEXT_MAX, never strlen). Per fact: support_count = usable same-key
 * records; confidence_x100 = % of those byte-identical to the chosen answer;
 * t_ms = the chosen record's; boot_id/seq left 0 (sem_store stamps at write).
 * Returns the fact count (0..max — capped at max), or -1 on bad args
 * (NULL recs with n>0, NULL out, max<=0 with work to do, min_support<1).
 * O(n^2) scan — distill-cadence use, never a hot path. */
int sd_distill(const epi_record_t *recs, int n, int min_support,
               semantic_fact_t *out, int max);

#endif /* SEMANTIC_DISTILL_H */
