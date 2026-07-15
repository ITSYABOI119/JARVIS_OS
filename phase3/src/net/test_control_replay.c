/* test_control_replay.c — host test for the PA-side replay-defense state machine. */
#include "control_replay.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int g_fail = 0;

#define CHECK(cond, msg) do {                                  \
    if (cond) { printf("PASS: %s\n", msg); }                   \
    else      { printf("FAIL: %s\n", msg); g_fail++; }         \
} while (0)

/* Build a distinct 16-byte nonce from a single tag byte. */
static void mk(uint8_t out[CONTROL_NONCE_LEN], uint8_t tag)
{
    unsigned i;
    for (i = 0; i < CONTROL_NONCE_LEN; i++) {
        out[i] = (uint8_t)(tag ^ (uint8_t)(i * 7u + 1u));
    }
}

static const char *vname(cr_verdict_t v)
{
    switch (v) {
    case CR_ACCEPT:       return "ACCEPT";
    case CR_REJECT_EPOCH: return "REJECT_EPOCH";
    case CR_REJECT_SEQ:   return "REJECT_SEQ";
    case CR_REJECT_NONCE: return "REJECT_NONCE";
    default:              return "??";
    }
}

/* One truth-table step: run check, assert the exact verdict, and assert the
 * floor is unchanged whenever the verdict is a reject. */
static void step(control_replay_t *st, uint64_t seq, uint32_t epoch, uint8_t tag,
                 cr_verdict_t want, const char *label)
{
    uint8_t n[CONTROL_NONCE_LEN];
    uint64_t floor_before = st->seq_floor;
    cr_verdict_t got;
    char buf[96];

    mk(n, tag);
    got = control_replay_check(st, seq, epoch, n);

    snprintf(buf, sizeof(buf), "%s -> %s (got %s)", label, vname(want), vname(got));
    CHECK(got == want, buf);

    if (want != CR_ACCEPT) {
        snprintf(buf, sizeof(buf), "%s: floor unchanged after REJECT", label);
        CHECK(st->seq_floor == floor_before, buf);
    }
}

int main(void)
{
    /* ---- Ordered truth table: current epoch=7, floor starts 0 ---- */
    control_replay_t st;
    control_replay_init(&st, 7);
    CHECK(st.seq_floor == 0 && st.boot_epoch == 7 &&
          st.nonce_count == 0 && st.nonce_head == 0, "init: floor=0 empty ring epoch=7");

    /* tags: A=0xA1 B=0xB2 C=0xC3 E=0xE5 F=0xF6 (all distinct) */
    step(&st, 1, 7, 0xA1, CR_ACCEPT,        "(1,7,A)");
    step(&st, 1, 7, 0xB2, CR_REJECT_SEQ,    "(1,7,B)");   /* seq fails before distinct nonce B */
    step(&st, 2, 7, 0xA1, CR_REJECT_NONCE,  "(2,7,A)");   /* seq ok, nonce A already seen */
    step(&st, 2, 7, 0xC3, CR_ACCEPT,        "(2,7,C)");
    step(&st, 9, 6, 0xE5, CR_REJECT_EPOCH,  "(9,6,E)");   /* prior-boot epoch */
    step(&st, 9, 8, 0xE5, CR_REJECT_EPOCH,  "(9,8,E)");   /* forged-future epoch */
    step(&st, 3, 7, 0xE5, CR_ACCEPT,        "(3,7,E)");
    step(&st, 0, 7, 0xF6, CR_REJECT_SEQ,    "(0,7,F)");   /* seq=0 <= floor */

    CHECK(st.seq_floor == 3, "final floor == 3 after truth table");

    /* ---- PERSISTENCE: only {seq_floor, boot_epoch} survive a reboot ---- */
    {
        struct { uint64_t seq_floor; uint32_t boot_epoch; } blob;
        control_replay_t reloaded;
        uint8_t nE[CONTROL_NONCE_LEN];
        cr_verdict_t v;

        blob.seq_floor  = st.seq_floor;   /* 3 */
        blob.boot_epoch = st.boot_epoch;  /* 7 */

        memset(&st, 0, sizeof(st));       /* wipe volatile RAM state */

        /* Reload: floor+epoch restored, nonce ring empty (not persisted). */
        control_replay_init(&reloaded, blob.boot_epoch);
        reloaded.seq_floor = blob.seq_floor;

        /* An old frame (seq=3, the last-accepted seq) still rejects on seq,
         * proving the persisted floor defends across reboot even though the
         * nonce ring was lost. */
        mk(nE, 0xE5);
        v = control_replay_check(&reloaded, 3, 7, nE);
        CHECK(v == CR_REJECT_SEQ, "persistence: old frame rejects after reload (REJECT_SEQ)");
        CHECK(reloaded.seq_floor == 3, "persistence: floor unchanged after the reject");

        /* A genuinely newer frame still works post-reboot. */
        v = control_replay_check(&reloaded, 4, 7, nE);
        CHECK(v == CR_ACCEPT, "persistence: newer frame accepts after reload");
    }

    /* ---- N-BOUNDARY: finite window, honestly documented ---- */
    {
        control_replay_t w;
        uint32_t i;
        uint8_t n0[CONTROL_NONCE_LEN], n_extra[CONTROL_NONCE_LEN];
        cr_verdict_t v;

        control_replay_init(&w, 1);

        /* Fill exactly N distinct nonces with advancing seq (1..N).
         * Tag 0 is nonce #1 (the first inserted). */
        for (i = 0; i < CONTROL_NONCE_WINDOW; i++) {
            uint8_t n[CONTROL_NONCE_LEN];
            mk(n, (uint8_t)(i & 0xFF));
            /* All tags 0..N-1 distinct for N=128 (fits a byte). */
            v = control_replay_check(&w, (uint64_t)(i + 1u), 1, n);
            if (v != CR_ACCEPT) { g_fail++; printf("FAIL: fill step %u got %s\n", i, vname(v)); }
        }
        CHECK(w.nonce_count == CONTROL_NONCE_WINDOW, "N-boundary: ring saturated at WINDOW");
        CHECK(w.seq_floor == CONTROL_NONCE_WINDOW, "N-boundary: floor == N after fill");

        /* Insert one more distinct nonce at seq N+1 -> evicts nonce #1. */
        mk(n_extra, 0xFE);
        v = control_replay_check(&w, (uint64_t)(CONTROL_NONCE_WINDOW + 1u), 1, n_extra);
        CHECK(v == CR_ACCEPT, "N-boundary: N+1th distinct nonce accepts, evicts #1");

        /* Nonce #1 (tag 0) is now evicted from the ring. Replaying it at a STALE
         * seq still rejects on the monotonic seq floor (the primary guarantee). */
        mk(n0, 0x00);
        v = control_replay_check(&w, 5, 1, n0);
        CHECK(v == CR_REJECT_SEQ,
              "N-boundary: evicted #1 at stale seq still rejects (seq floor is primary)");

        /* The evicted nonce re-ACCEPTS only when its seq has ALSO advanced past
         * the floor — the honest finite-window property. */
        v = control_replay_check(&w, (uint64_t)(CONTROL_NONCE_WINDOW + 2u), 1, n0);
        CHECK(v == CR_ACCEPT,
              "N-boundary: evicted #1 re-accepts only with an advanced seq (finite window)");
    }

    if (g_fail == 0) {
        printf("\nALL PASS\n");
        return 0;
    }
    printf("\n%d FAILURE(S)\n", g_fail);
    return 1;
}
