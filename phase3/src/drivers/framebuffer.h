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

#endif /* JARVIS_FRAMEBUFFER_H */
