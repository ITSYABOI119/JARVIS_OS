/*
 * Host test for the framebuffer blitter + 8x16 font (Phase 4 goal #2 Step 2c-1).
 * Links framebuffer.c directly — NO mocks (it is pure C). The blitter is seL4-only
 * at runtime, but its math/font are host-testable. Build:
 *   gcc -Wall -Werror -O2 -std=c11 test_framebuffer.c framebuffer.c -o test_framebuffer
 */
#include <stdio.h>
#include <stdint.h>
#include "framebuffer.h"

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

    printf("Framebuffer tests: %d PASS, %d FAIL\n", pass, fail);
    return fail ? 1 : 0;
}
