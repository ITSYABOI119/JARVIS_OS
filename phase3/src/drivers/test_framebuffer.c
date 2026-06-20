/*
 * Host test for the framebuffer blitter + 8x16 font + scrolling event-log
 * (Phase 4 goal #2 Steps 2c-1 / 2c-2b). Links framebuffer.c directly — NO mocks
 * (pure C). The blitter is seL4-only at runtime, but its math/font/log-ring are
 * host-testable. framebuffer.c #includes jarvis_ui_tokens.h, so add its dir. Build:
 *   gcc -Wall -Werror -O2 -std=c11 -I ../sel4 test_framebuffer.c framebuffer.c -o test_framebuffer
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "framebuffer.h"
#include "jarvis_ui_tokens.h"

static int pass = 0, fail = 0;
#define EXPECT(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fail++; } \
    else { printf("PASS: %s\n", msg); pass++; } \
} while (0)

#define FBW     64u
#define FBH     32u
#define FBPITCH 256u             /* width*4, 4-aligned */
#define STRIDE  (FBPITCH / 4u)   /* pixels per scanline */
#define NPIX    (FBW * FBH)      /* the FB covers exactly [0, NPIX) words here */

static uint32_t buf[NPIX + 1];   /* one guard word immediately after the FB region */
#define GUARD (NPIX)

static void sentinel(void) { for (uint32_t i = 0; i < NPIX + 1u; i++) buf[i] = 0xDEADBEEFu; }
static int  intact(void)   { for (uint32_t i = 0; i < NPIX + 1u; i++) if (buf[i] != 0xDEADBEEFu) return 0; return 1; }

int main(void)
{
    /* T1 — fb_init accept/reject */
    EXPECT(fb_init(buf, FBPITCH, FBW, FBH, 32) == 0,        "T1 init 64x32x32 pitch256 -> 0");
    EXPECT(fb_init(buf, FBPITCH, FBW, FBH, 24) == -1,       "T1 reject bpp=24");
    EXPECT(fb_init(buf, FBW * 4u - 1u, FBW, FBH, 32) == -1, "T1 reject pitch < width*4");
    EXPECT(fb_init(buf, FBW * 4u + 1u, FBW, FBH, 32) == -1, "T1 reject pitch not 4-aligned");
    EXPECT(fb_init(NULL, FBPITCH, FBW, FBH, 32) == -1,      "T1 reject NULL vaddr");
    EXPECT(fb_init(buf, FBPITCH, 0, FBH, 32) == -1,         "T1 reject width=0");

    /* T2 — putpixel + rb_swap */
    sentinel(); fb_init(buf, FBPITCH, FBW, FBH, 32); fb_set_rb_swap(0);
    fb_putpixel(0, 0, 0x00FF0000u);
    EXPECT(buf[0] == 0x00FF0000u, "T2 putpixel no-swap stores 0x00FF0000");
    fb_set_rb_swap(1);
    fb_putpixel(1, 0, 0x00FF0000u);
    EXPECT(buf[1] == 0x000000FFu, "T2 putpixel rb_swap stores 0x000000FF");
    fb_set_rb_swap(0);

    /* T3 — OOB putpixel writes nothing */
    sentinel(); fb_init(buf, FBPITCH, FBW, FBH, 32);
    fb_putpixel(FBW, 0, 0x11111111u);    /* x == width  */
    fb_putpixel(0, FBH, 0x22222222u);    /* y == height */
    EXPECT(intact(), "T3 OOB putpixel writes nothing");

    /* T4 — fill_rect clamps; guard word untouched */
    sentinel(); fb_init(buf, FBPITCH, FBW, FBH, 32);
    fb_fill_rect(2, 2, 1000, 1000, 0x00112233u);
    EXPECT(buf[(FBH - 1u) * STRIDE + (FBW - 1u)] == 0x00112233u, "T4 fill_rect clamp paints (w-1,h-1)");
    EXPECT(buf[GUARD] == 0xDEADBEEFu,                            "T4 fill_rect clamp: guard untouched");

    /* T5 — draw_char pixels match the font table */
    sentinel(); fb_init(buf, FBPITCH, FBW, FBH, 32);
    fb_draw_char(0, 0, 'A', 0x00FFFFFFu, 0x00000000u);
    {
        const uint8_t *gl = fb_font_glyph('A');
        int ok = 1;
        for (uint32_t r = 0; r < FB_FONT_H && ok; r++)
            for (uint32_t c = 0; c < FB_FONT_W && ok; c++) {
                uint32_t want = ((gl[r] >> (7u - c)) & 1u) ? 0x00FFFFFFu : 0x00000000u;
                if (buf[r * STRIDE + c] != want) ok = 0;
            }
        EXPECT(ok, "T5 draw_char('A') matches font8x16 glyph bits");
    }

    /* T6 — draw_char clipping (no OOB) */
    sentinel(); fb_init(buf, FBPITCH, FBW, FBH, 32);
    fb_draw_char(FBW, 0, 'A', 0x00FFFFFFu, 0x00000000u);       /* fully off-screen */
    EXPECT(intact(), "T6 draw_char at x==width writes nothing");
    sentinel(); fb_init(buf, FBPITCH, FBW, FBH, 32);
    fb_draw_char(FBW - 4u, 0, 'A', 0x00FFFFFFu, 0x00000000u);  /* half-clipped at right edge */
    EXPECT(buf[GUARD] == 0xDEADBEEFu, "T6 draw_char half-clipped: guard untouched");

    /* T7 — draw_text advances 8px/char ('i' lands at x=8) */
    sentinel(); fb_init(buf, FBPITCH, FBW, FBH, 32);
    fb_draw_text(0, 16, "Hi", 0x00FFFFFFu, 0x00000000u);
    {
        const uint8_t *gi = fb_font_glyph('i');
        int ok = 1;
        for (uint32_t r = 0; r < FB_FONT_H && ok; r++)
            for (uint32_t c = 0; c < FB_FONT_W && ok; c++) {
                uint32_t want = ((gi[r] >> (7u - c)) & 1u) ? 0x00FFFFFFu : 0x00000000u;
                if (buf[(16u + r) * STRIDE + 8u + c] != want) ok = 0;
            }
        EXPECT(ok, "T7 draw_text advance==8: 'i' at x=8");
    }

    /* T8 — event-log ring: append / wrap / truncate (ring logic, FB-independent) */
    {
        fb_log_reset();
        char nm[8];
        for (int i = 0; i < 28; i++) {           /* 28 appends into a JUI_LOG_H_ROWS(26)-row ring */
            snprintf(nm, sizeof nm, "L%d", i);
            fb_log_append(nm);
        }
        EXPECT(fb_log_count() == 28u,                 "T8 log_count==28 after 28 appends");
        EXPECT(strcmp(fb_log_visible(0),  "L2")  == 0, "T8 chrono: visible(0)==L2 (oldest shown)");
        EXPECT(strcmp(fb_log_visible(1),  "L3")  == 0, "T8 chrono: visible(1)==L3");
        EXPECT(strcmp(fb_log_visible(24), "L26") == 0, "T8 chrono: visible(24)==L26");
        EXPECT(strcmp(fb_log_visible(25), "L27") == 0, "T8 chrono: visible(25)==L27 (newest, bottom)");
        EXPECT(strcmp(fb_log_row(0), "L26") == 0,      "T8 raw slot: row0==L26 (back-compat)");
        char longln[200];
        for (int i = 0; i < 199; i++) longln[i] = 'x';
        longln[199] = '\0';
        fb_log_append(longln);                    /* append #29 lands at row 28%26 == 2 */
        EXPECT(strlen(fb_log_row(28u % JUI_LOG_H_ROWS)) == JUI_LOG_W_COLS,
               "T8 over-long line truncated to JUI_LOG_W_COLS");
    }

    /* T8b — partial fill: lines fill top-down (oldest at row 0), blanks below */
    {
        fb_log_reset();
        fb_log_append("first"); fb_log_append("second"); fb_log_append("third");
        EXPECT(strcmp(fb_log_visible(0), "first") == 0, "T8b partial: visible(0)==first (top/oldest)");
        EXPECT(strcmp(fb_log_visible(2), "third") == 0, "T8b partial: visible(2)==third (newest)");
        EXPECT(fb_log_visible(3)[0] == '\0',            "T8b partial: visible(3)=='' (blank below)");
    }

    /* T9 — event-log render lands at the right grid cell; '>' cursor; no OOB.
     * Needs an FB tall enough to contain the first log row (JUI_LOG_ROW*16). */
    {
        static uint32_t LBUF[128u * 256u + 1u];
        const uint32_t LSTRIDE = 128u, LGUARD = 128u * 256u;
        for (uint32_t i = 0; i < 128u * 256u + 1u; i++) LBUF[i] = 0xDEADBEEFu;
        EXPECT(fb_init(LBUF, 128u * 4u, 128u, 256u, 32) == 0, "T9 init 128x256 log FB");
        fb_set_rb_swap(0);
        fb_log_reset();
        fb_log_append("A");                        /* head row 0, with '>' cursor */
        uint32_t ty = JUI_LOG_ROW * JUI_CELL_H;                 /* row 0 = py 224 */
        uint32_t tx = (JUI_LOG_COL + 2u) * JUI_CELL_W;          /* text after 2-char gutter */
        const uint8_t *gA = fb_font_glyph('A');
        int ok = 1;
        for (uint32_t r = 0; r < FB_FONT_H && ok; r++)
            for (uint32_t c = 0; c < FB_FONT_W && ok; c++) {
                uint32_t want = ((gA[r] >> (7u - c)) & 1u) ? JCLR_TEXT2 : FBP_BG;
                if (LBUF[(ty + r) * LSTRIDE + tx + c] != want) ok = 0;
            }
        EXPECT(ok, "T9 head text 'A' at (col 4, row 14) in JCLR_TEXT2");
        uint32_t gx = JUI_LOG_COL * JUI_CELL_W;
        const uint8_t *gcur = fb_font_glyph('>');
        int cok = 1;
        for (uint32_t r = 0; r < FB_FONT_H && cok; r++)
            for (uint32_t c = 0; c < FB_FONT_W && cok; c++) {
                uint32_t want = ((gcur[r] >> (7u - c)) & 1u) ? JCLR_LIVE : FBP_BG;
                if (LBUF[(ty + r) * LSTRIDE + gx + c] != want) cok = 0;
            }
        EXPECT(cok, "T9 '>' head cursor at (col 2, row 14) in JCLR_LIVE");
        EXPECT(LBUF[LGUARD] == 0xDEADBEEFu, "T9 log render: guard word untouched (no OOB)");
    }

    /* T9b — natural-order render: oldest at top, newest pinned to the BOTTOM row */
    {
        static uint32_t LB2[128u * 640u + 1u];   /* 640 = (JUI_LOG_ROW+JUI_LOG_H_ROWS)*16 (incl. bottom row) */
        const uint32_t S2 = 128u, G2 = 128u * 640u;
        for (uint32_t i = 0; i < 128u * 640u + 1u; i++) LB2[i] = 0xDEADBEEFu;
        EXPECT(fb_init(LB2, 128u * 4u, 128u, 640u, 32) == 0, "T9b init 128x640 log FB");
        fb_set_rb_swap(0);
        fb_log_reset();
        char nm[8];
        for (int i = 0; i < 30; i++) { snprintf(nm, sizeof nm, "L%d", i); fb_log_append(nm); }  /* 30 > 26 rows */
        EXPECT(strcmp(fb_log_visible(0),  "L4")  == 0, "T9b chrono: visible(0)==L4 (oldest shown)");
        EXPECT(strcmp(fb_log_visible(25), "L29") == 0, "T9b chrono: visible(25)==L29 (newest)");
        uint32_t bx = JUI_LOG_COL * JUI_CELL_W;
        uint32_t by = (JUI_LOG_ROW + 25u) * JUI_CELL_H;     /* bottom screen row 25 = py624 */
        const uint8_t *gc = fb_font_glyph('>');
        int bok = 1;
        for (uint32_t r = 0; r < FB_FONT_H && bok; r++)
            for (uint32_t c = 0; c < FB_FONT_W && bok; c++) {
                uint32_t want = ((gc[r] >> (7u - c)) & 1u) ? JCLR_LIVE : FBP_BG;
                if (LB2[(by + r) * S2 + bx + c] != want) bok = 0;
            }
        EXPECT(bok, "T9b '>' cursor on BOTTOM row 25 (py624) in JCLR_LIVE");
        uint32_t ty0 = JUI_LOG_ROW * JUI_CELL_H;            /* top screen row 0 */
        int spok = 1;
        for (uint32_t r = 0; r < FB_FONT_H && spok; r++)
            for (uint32_t c = 0; c < FB_FONT_W && spok; c++)
                if (LB2[(ty0 + r) * S2 + bx + c] != FBP_BG) spok = 0;
        EXPECT(spok, "T9b top row 0 gutter is SPACE (no '>' cursor)");
        EXPECT(LB2[G2] == 0xDEADBEEFu, "T9b guard word untouched (no OOB)");
    }

    printf("Framebuffer tests: %d PASS, %d FAIL\n", pass, fail);
    return fail ? 1 : 0;
}
