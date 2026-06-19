/*
 * JARVIS AI-OS — minimal linear-framebuffer blitter (Phase 4 goal #2 Step 2b).
 * See framebuffer.h. 32bpp linear, pitch-addressed, uncacheable target.
 */
#include "framebuffer.h"

static volatile uint8_t *g_fb     = 0;
static uint32_t          g_pitch  = 0;   /* bytes per scanline (>= width*4) */
static uint32_t          g_width  = 0;   /* visible pixels per scanline */
static uint32_t          g_height = 0;
static uint8_t           g_bpp    = 0;
static int               g_rb_swap = 0;

int fb_init(void *vaddr, uint32_t pitch, uint32_t width, uint32_t height, uint8_t bpp)
{
    /* 32bpp only; pitch must cover the visible width (often padded larger) and be
     * 4-byte aligned (every row pointer is cast to uint32_t* for the pixel stores). */
    if (!vaddr || bpp != 32 || width == 0 || height == 0 ||
        pitch < width * 4u || (pitch & 3u)) {
        return -1;
    }
    g_fb      = (volatile uint8_t *)vaddr;
    g_pitch   = pitch;
    g_width   = width;
    g_height  = height;
    g_bpp     = bpp;
    g_rb_swap = 0;
    return 0;
}

int  fb_ready(void)         { return g_fb != 0; }
void fb_set_rb_swap(int on) { g_rb_swap = on ? 1 : 0; }

/* Map a 0x00RRGGBB color to the 32-bit value stored at a pixel.
 * Default: store as-is (correct for B,G,R,X little-endian memory order).
 * Swap mode: exchange R and B (for R,G,B,X memory order). */
static inline uint32_t fb_pack(uint32_t rgb)
{
    rgb &= 0x00FFFFFFu;
    if (g_rb_swap) {
        return ((rgb & 0x0000FFu) << 16) | (rgb & 0x00FF00u) | ((rgb >> 16) & 0x0000FFu);
    }
    return rgb;
}

void fb_putpixel(uint32_t x, uint32_t y, uint32_t rgb)
{
    if (!g_fb || x >= g_width || y >= g_height) return;
    volatile uint32_t *px =
        (volatile uint32_t *)(g_fb + (uintptr_t)y * g_pitch + (uintptr_t)x * 4u);
    *px = fb_pack(rgb);
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb)
{
    if (!g_fb || x >= g_width || y >= g_height) return;
    uint32_t x1 = (w > g_width  - x) ? g_width  : x + w;   /* clamp, overflow-safe */
    uint32_t y1 = (h > g_height - y) ? g_height : y + h;
    uint32_t v  = fb_pack(rgb);
    for (uint32_t yy = y; yy < y1; yy++) {
        volatile uint32_t *row =
            (volatile uint32_t *)(g_fb + (uintptr_t)yy * g_pitch);
        for (uint32_t xx = x; xx < x1; xx++) {
            row[xx] = v;
        }
    }
}

void fb_clear(uint32_t rgb)
{
    fb_fill_rect(0, 0, g_width, g_height, rgb);
}

void fb_flush(void)
{
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile ("sfence" ::: "memory");
#else
    __sync_synchronize();
#endif
}
