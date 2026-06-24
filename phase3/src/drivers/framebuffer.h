#ifndef JARVIS_FRAMEBUFFER_H
#define JARVIS_FRAMEBUFFER_H

#include <stdint.h>

/*
 * JARVIS AI-OS — minimal linear-framebuffer blitter (Phase 4 goal #2 Step 2b).
 *
 * Draws into the firmware GOP framebuffer that seL4 forwards as multiboot2
 * bootinfo id=4. The rootserver maps that framebuffer UNCACHEABLE (a
 * WriteBack/cacheable mapping would not reliably reach VRAM — the GPU scanout
 * does not snoop the CPU cache; UC is MTRR-independent and guaranteed visible.
 * True WriteCombining would be faster — a Step 2c perf pass). 32bpp only for
 * now; no font/text yet (Step 2c).
 *
 * Byte order: the id=4 descriptor gives `type` (1 = direct RGB) but NOT the
 * channel masks. Colors are passed as 0x00RRGGBB. On the common GOP memory
 * layout (bytes B,G,R,X) a little-endian 0x00RRGGBB store renders correctly.
 * If the test pattern's "RED" rect shows as blue, the framebuffer is the
 * opposite (R,G,B,X) order — call fb_set_rb_swap(1) to swap R<->B at store time.
 *
 * NOTE: seL4-only (drives a mapped device framebuffer); no host unit test.
 */

/* Returns 0 on success, -1 if args are invalid (bpp MUST be 32, pitch>=width*4). */
int  fb_init(void *vaddr, uint32_t pitch, uint32_t width, uint32_t height, uint8_t bpp);
int  fb_ready(void);                 /* 1 once fb_init has succeeded */
void fb_set_rb_swap(int on);         /* 1 = swap R<->B at store (R,G,B,X framebuffers) */

void fb_putpixel(uint32_t x, uint32_t y, uint32_t rgb);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb);
void fb_clear(uint32_t rgb);
void fb_flush(void);                 /* sfence — orders store buffers (harmless no-op for UC) */

/* 8x16 bitmap-font text (Step 2c-1). Colors go through fb_pack (rb_swap honored).
 * Glyphs are clipped at the framebuffer edges — never writes out of bounds. */
#define FB_FONT_W 8u
#define FB_FONT_H 16u
void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void fb_draw_text(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t bg);

/* 16-byte glyph bitmap for c (printable 0x20–0x7E; out-of-range → space glyph).
 * Exposed for the host test. Each byte = one scanline, bit 0x80 = leftmost pixel. */
const uint8_t *fb_font_glyph(char c);

/* Horizontal progress bar (goal #2 backlog: live model-load %). A track rect with
 * a 1px border (all 4 edges) and a left-anchored fill spanning (w-2)*pct/100 of the
 * inner width. pct is clamped to [0,100]. Pure / UC-safe / edge-clipped (built only
 * from fb_fill_rect). No-op if the FB is not ready or w<2/h<2 (no room for border). */
void fb_progress_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                     uint8_t pct, uint32_t fill_rgb, uint32_t track_rgb, uint32_t border_rgb);

/* ---- Step 2c-2b/2c-2c: scrolling event-log region (natural chronological order) ----
 * A ring of the last JUI_LOG_H_ROWS completed serial lines (geometry/colors from
 * jarvis_ui_tokens.h). The UC framebuffer is never memmove'd: each append repaints
 * the whole window in place — OLDEST at the top row, NEWEST at the bottom, '>' on
 * the newest. fb_log_append updates the ring even with no framebuffer (host-testable)
 * and only draws when fb_ready(). */
void        fb_log_reset(void);              /* clear ring + draw the region frame   */
void        fb_log_append(const char *line); /* append one completed line + repaint   */
uint32_t    fb_log_count(void);              /* total lines appended (test accessor)  */
const char *fb_log_row(uint32_t row);        /* raw ring slot (test/back-compat)      */
const char *fb_log_visible(uint32_t screen_row); /* line as displayed: 0=oldest..vis-1=newest */

#endif /* JARVIS_FRAMEBUFFER_H */
