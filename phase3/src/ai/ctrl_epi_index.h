/*
 * ctrl_epi_index.h — Phase 6 A9/1: control-IN recall-index maintenance, host-pure.
 *
 * WHY THIS EXISTS. Whether a control-IN turn is RECALLABLE is decided in two places that must
 * agree: the write path (ctrl_epi_write, after a successful epi_store_append) and the boot scan
 * that rebuilds the index from stored bytes. A third place — pa_ctrl_gate's post-fetch guard —
 * re-applies the same predicate to whatever the lookup returned. Today all three restate the
 * decision separately, and the failure when they disagree is silent and permanent:
 * epi_index_lookup is NEWEST-WINS, returns a SINGLE logical index, and has NO fallback to the
 * next-newest entry for the same key. So a record that the index accepts but the fetch filter
 * rejects does not degrade to "try the one before it" — it SHADOWS the good prior answer under
 * that key, and that question reads hit=0 forever.
 *
 * That is not hypothetical. The write path's predicate was once a hand-written restatement that
 * omitted `action` entirely, which was harmless only while every record in this store was tag 3;
 * EPI_ACT_CONTROL_IN_LOCAL (tag 4) made it a live defect, and the 6-6 routing false-positive
 * question is a real key with both tags under it.
 *
 * THE DESIGN WIN, and it is the whole point of the extraction: after this, both maintenance sites
 * go through ctrl_index_add(), which CALLS g3_candidate_usable rather than restating it. "The
 * index and the fetch filter cannot disagree" stops being a comment asking future readers to keep
 * two copies in step and becomes structural — one predicate, one place. Same reasoning as the
 * PB_DISPATCH_OK() centralization: the guard that must hold at every lane is spelled once.
 *
 * BEHAVIOUR-NEUTRAL EXTRACTION. Every rule here is today's rule, deliberately including the ones
 * that look like they could be tightened. main_x86.c is seL4-only and is NEVER host-compiled, so
 * this logic currently has no test at all; extracting it follows km2b_miss / km2b_trigger / wake /
 * control_floor / pb_progress / embed_region. Host-tested by test_ctrl_epi_index.c.
 *
 * *** WHAT THIS MODULE DOES NOT PROMISE — read before trusting a lookup. ***
 * A logical index is a position in the store's oldest->newest ordering, NOT a stable handle. Once
 * the region rolls, epi_store_read maps logical L to slot (cursor + L) % max, so every existing
 * index entry silently starts pointing at a DIFFERENT record. This module cannot detect that: it
 * holds no store state and never reads the store outside a build.
 *
 * The CALLER is what makes a stale entry safe, by re-checking the fetched record:
 *
 *     int li = ctrl_index_lookup(&ix, ckey);
 *     if (li >= 0 && epi_store_read(&store, (uint32_t)li, &rec) == 0
 *         && rec.query_key == ckey                                      <-- the safety net
 *         && g3_candidate_usable(rec.action, rec.outcome, rec.resp_len,
 *                                EPI_ACT_CONTROL_IN, EPI_OUT_OK)) { ... }
 *
 * Both halves of that guard are required and neither is redundant. Host-measured in
 * test_ctrl_epi_index.c T5: after a wrap a lookup really does return an index whose record carries
 * a different query_key, and the re-check is what turns that into a clean MISS instead of a wrong
 * recall. The honest residual, also pinned in T5: when the drifted slot happens to hold ANOTHER
 * record for the SAME key, the re-check passes and the caller recalls a valid but OLDER answer to
 * the right question. That is a freshness limit, not a wrong recall — but it is a real one, and it
 * is the reason the guard is described as a safety net rather than a correctness proof.
 */

#ifndef CTRL_EPI_INDEX_H
#define CTRL_EPI_INDEX_H

#include <stdint.h>
#include "episodic_store.h"   /* epi_index_entry_t, epi_record_t, EPI_ACT_* / EPI_OUT_* */

/* The index handle. `ent` is caller-owned storage (a file-scope array in the deployed build), so
 * this module allocates nothing and can be dropped straight into PA. `n` is the live entry count
 * and is what ctrl_index_lookup scans. */
typedef struct {
    epi_index_entry_t *ent;   /* caller-owned; NULL is tolerated (cap is forced to 0) */
    int                cap;   /* entries `ent` can hold */
    int                n;     /* entries currently held */
} ctrl_epi_index_t;

/* Point the handle at caller-owned storage and empty it.
 * A NULL `storage` or a negative `cap` forces cap = 0, so every later add reports "no room"
 * rather than writing through a bad pointer. */
void ctrl_index_init(ctrl_epi_index_t *ix, epi_index_entry_t *storage, int cap);

/* Empty the index without touching the storage pointer or cap. The boot rebuild starts here, so
 * a second build replaces the first rather than appending to it. */
void ctrl_index_reset(ctrl_epi_index_t *ix);

/*
 * Offer one just-stored record to the index at its logical position.
 *
 *   1 = ADDED         — recallable, and there was room
 *   0 = NOT RECALLABLE — g3_candidate_usable said no (a failed turn, an empty response, or a
 *                        locally-served tag-4 turn); also the fail-safe answer for a NULL/invalid
 *                        handle or record
 *  -1 = INDEX FULL     — the record IS recallable, but n == cap so nothing was written
 *
 * THREE VALUES, NOT TWO, and the distinction is load-bearing at the call site. Today's code
 * computes `idxable` from the predicate ALONE and gates two separate things on it: the embed-on-
 * write publish (a record worth ~2 s of embedding only if it is recallable) and the one-shot
 * "index FULL" warning. The bounds check gates only the array write. So a full index must still
 * report "recallable" — collapsing -1 into 0 would silently stop both the embed and the warning
 * on a saturated box, which is exactly when the warning matters. Callers reproducing today's
 * behaviour want `idxable = (rc != 0)`.
 *
 * A NULL handle or record returns 0 rather than a distinct error code, deliberately: 0 is the
 * fail-safe value for that same `idxable` test. An invalid handle must not read as "recallable
 * but full", which would license an embed and a full-index warning for a record that was never
 * examined.
 *
 * *** THE UNDERFLOW GUARD LIVES AT THE CALL SITE, NOT HERE. *** The write path derives the
 * logical index as `epi_store_count() - 1`, which underflows to 0xFFFFFFFF when the count is 0.
 * Today that is guarded by the `(cnt > 0) &&` term in front of the predicate. This function takes
 * an already-computed logical_index and cannot re-derive that check, so the caller MUST keep it.
 * Stated loudly because the extraction moves it: a caller that drops it would index a phantom
 * record at 0xFFFFFFFF, which no read can ever satisfy.
 *
 * The action/outcome tags are hardcoded to EPI_ACT_CONTROL_IN / EPI_OUT_OK rather than passed in.
 * This is the CONTROL-IN index; both of today's sites hardcode them identically, and making them
 * parameters would invite exactly the divergence the module exists to prevent.
 */
int ctrl_index_add(ctrl_epi_index_t *ix, const epi_record_t *rec, uint32_t logical_index);

/* Read one stored record by logical index into `out`. 0 = success, non-zero = skip this record
 * (matching epi_store_read's convention, which the deployed caller wraps directly). */
typedef int (*ctrl_index_read_fn)(void *ctx, uint32_t logical_index, epi_record_t *out);

/*
 * Rebuild the whole index from the store: reset, then walk logical 0..store_count-1 oldest-first,
 * offering every successfully-read record to ctrl_index_add. Returns the number of entries indexed
 * (== ix->n). A record that fails to read is skipped, exactly as today.
 *
 * The walk is CLAMPED to ix->cap. In the deployed build the store's max_entries and the index
 * array are both CTRL_EPI_MAX_ENTRIES, so the clamp is behaviour-identical to today's
 * `if (_ccnt > CTRL_EPI_MAX_ENTRIES) _ccnt = CTRL_EPI_MAX_ENTRIES;` — and in fact neither can fire,
 * because epi_store_count() already caps at the store's own max_entries. It is kept because it is
 * what makes the build safe if those two bounds ever diverge: today's boot scan has NO per-write
 * bounds check and relies entirely on walk <= array size. Routing the build through
 * ctrl_index_add gives it that check for free.
 *
 * APPEND ORDER IS PART OF THE CONTRACT, not an accident. Entries are appended oldest-first, and
 * ctrl_sem_gather walks the array BACKWARDS to get newest-first candidates. A build that reordered
 * entries would silently invert semantic candidate priority.
 *
 * `scratch` is a caller-supplied 512-byte record buffer. It is a parameter rather than a local so
 * the extraction stays exactly behaviour-neutral: the deployed boot scan reads into the file-scope
 * g_ctrl_recall_rec, and a local would both move 512 B onto a stack frame that never carried it
 * and stop that global being written. NULL scratch (or NULL read_rec) leaves the index reset and
 * empty and returns 0 — a build that cannot read anything indexes nothing.
 */
int ctrl_index_build(ctrl_epi_index_t *ix, uint32_t store_count,
                     ctrl_index_read_fn read_rec, void *ctx, epi_record_t *scratch);

/*
 * Newest entry for `key` -> its logical_index, or -1 on miss/empty/NULL.
 *
 * Delegates to epi_index_lookup rather than reimplementing the scan — a second copy of
 * "newest-wins" is a second thing to keep in step, which is the defect class this module exists to
 * remove. Note what "newest" means there: the HIGHEST logical_index among ALL matching entries, a
 * value comparison, not array order and not last-added. The two coincide while entries are appended
 * in ascending logical order (which build guarantees and the write path preserves), and the value
 * comparison is what still does the right thing if they ever do not.
 *
 * See the header comment: the returned index may be STALE after the store rolls. The caller must
 * re-check the fetched record's query_key.
 */
int ctrl_index_lookup(const ctrl_epi_index_t *ix, uint64_t key);

#endif /* CTRL_EPI_INDEX_H */
