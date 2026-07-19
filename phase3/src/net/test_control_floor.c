/* test_control_floor.c — Phase 6 6-5/M3-3 host test for control_floor.c (pure logic). */
#include "control_floor.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int pass = 0, fail = 0;
#define CHECK(c, msg) do { \
    if (c) { pass++; } \
    else   { fail++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

int main(void)
{
    printf("== test_control_floor ==\n");

    /* ---- T1: build -> parse round-trip (floor + wc exact) ---- */
    {
        uint8_t buf[512];
        ctrl_floor_build(buf, 12345ULL, 7u);
        uint64_t f = 0; uint32_t wc = 0;
        int ok = ctrl_floor_parse(buf, &f, &wc);
        CHECK(ok == 1, "round-trip parse ok");
        CHECK(f == 12345ULL, "round-trip floor exact");
        CHECK(wc == 7u, "round-trip wc exact");
        /* NULL out-params are safe */
        CHECK(ctrl_floor_parse(buf, NULL, NULL) == 1, "parse NULL out-params");
    }

    /* ---- T2: a large u64 floor survives (no truncation) ---- */
    {
        uint8_t buf[512];
        uint64_t big = 0x0123456789ABCDEFULL;
        ctrl_floor_build(buf, big, 0xFFFFFFF0u);
        uint64_t f = 0; uint32_t wc = 0;
        CHECK(ctrl_floor_parse(buf, &f, &wc) == 1, "big-floor parse ok");
        CHECK(f == big, "big-floor exact u64");
        CHECK(wc == 0xFFFFFFF0u, "big-wc exact u32");
    }

    /* ---- T3: checksum catches a 1-bit flip in EACH covered field ---- */
    {
        uint8_t buf[512];
        ctrl_floor_build(buf, 1000ULL, 3u);
        /* flip a bit in reserved_floor (offset 8) */
        uint8_t b0 = buf[8]; buf[8] ^= 0x01;
        CHECK(ctrl_floor_parse(buf, NULL, NULL) == 0, "checksum catches floor bit-flip");
        buf[8] = b0;
        CHECK(ctrl_floor_parse(buf, NULL, NULL) == 1, "restore -> valid again");
        /* flip a bit in write_count (offset 16) */
        buf[16] ^= 0x80;
        CHECK(ctrl_floor_parse(buf, NULL, NULL) == 0, "checksum catches wc bit-flip");
        /* flip a bit in the checksum field itself (offset 20) */
        ctrl_floor_build(buf, 1000ULL, 3u);
        buf[20] ^= 0x01;
        CHECK(ctrl_floor_parse(buf, NULL, NULL) == 0, "flipped checksum field -> reject");
        /* wrong magic / wrong version */
        ctrl_floor_build(buf, 1000ULL, 3u);
        buf[0] ^= 0xFF;
        CHECK(ctrl_floor_parse(buf, NULL, NULL) == 0, "wrong magic -> reject");
        ctrl_floor_build(buf, 1000ULL, 3u);
        buf[4] = 2;   /* version 1 -> 2 */
        CHECK(ctrl_floor_parse(buf, NULL, NULL) == 0, "wrong version -> reject (would need checksum too)");
    }

    /* ---- T4: all-zero sector -> parse=0; select(both-zero) -> FLOOR_FRESH ---- */
    {
        uint8_t z[512]; memset(z, 0, sizeof z);
        CHECK(ctrl_floor_parse(z, NULL, NULL) == 0, "all-zero sector parse=0");
        uint64_t f = 999; uint32_t wc = 999;
        CHECK(ctrl_floor_select(z, z, &f, &wc) == FLOOR_FRESH, "both-zero -> FLOOR_FRESH");
        CHECK(f == 0ULL, "FRESH floor=0");
        CHECK(wc == 1u, "FRESH next_wc=1");
    }

    /* ---- T5: select picks the HIGHER write_count of two valid sectors ---- */
    {
        uint8_t a[512], b[512];
        ctrl_floor_build(a, 500ULL, 10u);
        ctrl_floor_build(b, 700ULL, 11u);   /* newer */
        uint64_t f = 0; uint32_t wc = 0;
        CHECK(ctrl_floor_select(a, b, &f, &wc) == FLOOR_OK, "two-valid -> FLOOR_OK");
        CHECK(f == 700ULL, "picks newer floor (B)");
        CHECK(wc == 12u, "next_wc = winner_wc + 1");
        /* order-independent: A newer */
        ctrl_floor_build(a, 800ULL, 20u);
        ctrl_floor_build(b, 700ULL, 11u);
        CHECK(ctrl_floor_select(a, b, &f, &wc) == FLOOR_OK, "A-newer -> FLOOR_OK");
        CHECK(f == 800ULL && wc == 21u, "picks newer floor (A) + next_wc");
    }

    /* ---- T6: torn write — A valid + B garbage-nonzero -> A wins (the crux) ---- */
    {
        uint8_t a[512], b[512];
        ctrl_floor_build(a, 1234ULL, 5u);
        memset(b, 0xAB, sizeof b);   /* garbage, non-zero, invalid magic/checksum */
        uint64_t f = 0; uint32_t wc = 0;
        CHECK(ctrl_floor_select(a, b, &f, &wc) == FLOOR_OK, "A-valid B-garbage -> FLOOR_OK");
        CHECK(f == 1234ULL && wc == 6u, "torn: surviving A wins");
        /* symmetric: B valid + A garbage */
        memset(a, 0xCD, sizeof a);
        ctrl_floor_build(b, 4321ULL, 9u);
        CHECK(ctrl_floor_select(a, b, &f, &wc) == FLOOR_OK, "A-garbage B-valid -> FLOOR_OK");
        CHECK(f == 4321ULL && wc == 10u, "torn: surviving B wins");
    }

    /* ---- T7: both garbage-nonzero -> FLOOR_CORRUPT (fail-safe) ---- */
    {
        uint8_t a[512], b[512];
        memset(a, 0x11, sizeof a);
        memset(b, 0x22, sizeof b);
        CHECK(ctrl_floor_select(a, b, NULL, NULL) == FLOOR_CORRUPT, "both-garbage -> FLOOR_CORRUPT");
        /* one valid, one garbage is NOT corrupt (T6); one all-zero + one garbage: */
        uint8_t z[512]; memset(z, 0, sizeof z);
        CHECK(ctrl_floor_select(z, b, NULL, NULL) == FLOOR_CORRUPT,
              "zero + garbage -> CORRUPT (a real sector is torn, not a fresh box)");
    }

    /* ---- T8: write_count WRAP tie-break — 0 is newer than 0xFFFFFFFF ---- */
    {
        uint8_t a[512], b[512];
        ctrl_floor_build(a, 111ULL, 0xFFFFFFFFu);
        ctrl_floor_build(b, 222ULL, 0u);           /* wrapped -> newer */
        uint64_t f = 0; uint32_t wc = 0;
        CHECK(ctrl_floor_select(a, b, &f, &wc) == FLOOR_OK, "wrap pair -> FLOOR_OK");
        CHECK(f == 222ULL, "wrap: wc=0 newer than wc=0xFFFFFFFF");
        CHECK(wc == 1u, "wrap: next_wc = 0 + 1");
        /* symmetric */
        ctrl_floor_build(a, 111ULL, 0u);
        ctrl_floor_build(b, 222ULL, 0xFFFFFFFFu);
        CHECK(ctrl_floor_select(a, b, &f, &wc) == FLOOR_OK && f == 111ULL,
              "wrap symmetric: A wc=0 wins");
    }

    /* ---- T9: ctrl_floor_due — eager reservation ---- */
    {
        uint64_t nr = 0;
        /* not yet crossed */
        CHECK(ctrl_floor_due(100ULL, 257ULL, &nr) == 0, "seq below reservation -> not due");
        /* exactly at the reservation -> due */
        CHECK(ctrl_floor_due(257ULL, 257ULL, &nr) == 1, "seq == reservation -> due");
        CHECK(nr == 257ULL + CTRL_FLOOR_RESERVE, "new_reservation = seq + RESERVE");
        /* crossed */
        CHECK(ctrl_floor_due(300ULL, 257ULL, &nr) == 1, "seq above reservation -> due");
        CHECK(nr == 300ULL + CTRL_FLOOR_RESERVE, "new_reservation tracks the actual seq");
        /* first accept from a fresh floor (reservation 0) */
        CHECK(ctrl_floor_due(1ULL, 0ULL, &nr) == 1, "first accept (resv 0) -> due");
        CHECK(nr == 1ULL + CTRL_FLOOR_RESERVE, "fresh: reserve ahead of seq 1");
        /* NULL out-param safe */
        CHECK(ctrl_floor_due(5ULL, 1ULL, NULL) == 1, "due with NULL new_reservation");
        /* fail-high invariant: the new reservation is strictly > the seq */
        ctrl_floor_due(9999ULL, 9999ULL, &nr);
        CHECK(nr > 9999ULL, "new_reservation strictly ahead of the accepted seq");
    }

    /* ---- T10: next_lba alternates A/B by write_count parity ---- */
    {
        CHECK(ctrl_floor_next_lba(2u) == CTRL_FLOOR_LBA_A, "even wc -> A");
        CHECK(ctrl_floor_next_lba(3u) == CTRL_FLOOR_LBA_B, "odd wc -> B");
        CHECK(ctrl_floor_next_lba(0u) == CTRL_FLOOR_LBA_A, "wc 0 -> A");
        CHECK(CTRL_FLOOR_LBA_A == CTRL_KEY_BASE_LBA + 1ULL, "LBA_A layout");
        CHECK(CTRL_FLOOR_LBA_B == CTRL_KEY_BASE_LBA + 2ULL, "LBA_B layout");
        CHECK(CTRL_FLOOR_LBA_A != CTRL_FLOOR_LBA_B, "A != B (double buffer distinct)");
        /* consecutive writes (wc, wc+1) alternate sectors */
        CHECK(ctrl_floor_next_lba(6u) != ctrl_floor_next_lba(7u), "consecutive wc alternate LBAs");
    }

    printf("== %d PASS, %d FAIL ==\n", pass, fail);
    return fail ? 1 : 0;
}
