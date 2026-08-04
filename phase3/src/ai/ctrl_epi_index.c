/* Phase 6 A9/1 — control-IN recall-index maintenance, host-pure. See ctrl_epi_index.h.
 *
 * Deliberately thin: the value is not the code, it is that the recallability predicate is now
 * invoked from ONE place instead of being restated at the write path and the boot scan. Nothing
 * here decides what "recallable" means — g3_candidate_usable does, and this file calls it.
 *
 * Behaviour-neutral extraction of main_x86.c's two index-maintenance sites. Host-tested by
 * test_ctrl_epi_index.c. */

#include "ctrl_epi_index.h"
#include "g3_retrieval.h"   /* g3_candidate_usable — a static inline, so nothing extra to link */

void ctrl_index_init(ctrl_epi_index_t *ix, epi_index_entry_t *storage, int cap)
{
    if (!ix)
        return;
    ix->ent = storage;
    /* No storage means no capacity, whatever the caller claimed. Forcing cap to 0 turns a bad
     * handle into "no room" (-1 from add) instead of a write through a NULL pointer. */
    ix->cap = (storage && cap > 0) ? cap : 0;
    ix->n   = 0;
}

void ctrl_index_reset(ctrl_epi_index_t *ix)
{
    if (!ix)
        return;
    /* Only the count is cleared. Stale entries beyond n are unreachable — lookup scans [0, n) —
     * so wiping them would be work with no observable effect, and today's boot scan does not
     * wipe them either. */
    ix->n = 0;
}

int ctrl_index_add(ctrl_epi_index_t *ix, const epi_record_t *rec, uint32_t logical_index)
{
    if (!ix || !rec)
        return 0;   /* fail-safe: never "recallable but full" for a record we did not examine */

    /* THE predicate — called, never restated. This is the whole extraction. */
    if (!g3_candidate_usable(rec->action, rec->outcome, rec->resp_len,
                             EPI_ACT_CONTROL_IN, EPI_OUT_OK))
        return 0;

    /* Recallable from here on, so a full index reports -1 rather than 0: the caller still wants
     * to embed this record and still wants its one-shot "index FULL" warning. */
    if (ix->n >= ix->cap)
        return -1;

    ix->ent[ix->n].key           = rec->query_key;
    ix->ent[ix->n].logical_index = logical_index;
    ix->n++;
    return 1;
}

int ctrl_index_build(ctrl_epi_index_t *ix, uint32_t store_count,
                     ctrl_index_read_fn read_rec, void *ctx, epi_record_t *scratch)
{
    if (!ix)
        return 0;

    /* Reset FIRST, and unconditionally: a build always replaces, so a second build over a broken
     * reader must leave an empty index rather than the previous one. */
    ctrl_index_reset(ix);
    if (!read_rec || !scratch || ix->cap <= 0)
        return 0;

    uint32_t walk = store_count;
    if (walk > (uint32_t)ix->cap)
        walk = (uint32_t)ix->cap;

    /* Oldest-first: ctrl_sem_gather walks the result backwards for newest-first candidates. */
    for (uint32_t li = 0; li < walk; li++) {
        if (read_rec(ctx, li, scratch) != 0)
            continue;                       /* unreadable record — skipped, exactly as today */
        if (ctrl_index_add(ix, scratch, li) < 0)
            break;                          /* full; unreachable while walk <= cap */
    }
    return ix->n;
}

int ctrl_index_lookup(const ctrl_epi_index_t *ix, uint64_t key)
{
    if (!ix)
        return -1;
    /* epi_index_lookup already guards NULL entries / n <= 0, and owns the newest-wins rule. */
    return epi_index_lookup(ix->ent, ix->n, key);
}
