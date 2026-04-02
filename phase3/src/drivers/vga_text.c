#include "vga_text.h"

#ifdef JARVIS_TEST_MOCK
extern uint16_t jarvis_test_vga_buffer[];
static volatile uint16_t *vga_buffer = (volatile uint16_t *)jarvis_test_vga_buffer;
#else
static volatile uint16_t *vga_buffer = (volatile uint16_t *)VGA_ADDR;
#endif

static int vga_row = 0;
static int vga_col = 0;
static uint8_t vga_color = VGA_DEFAULT_COLOR;

void vga_set_buffer(void *mapped_addr) {
    vga_buffer = (volatile uint16_t *)mapped_addr;
}

void vga_init(void) {
    vga_row = 0;
    vga_col = 0;
    vga_color = VGA_DEFAULT_COLOR;
    vga_clear();
}

void vga_clear(void) {
    uint16_t blank = (uint16_t)(' ') | ((uint16_t)vga_color << 8);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga_buffer[i] = blank;
    vga_row = 0;
    vga_col = 0;
}

static void vga_scroll(void) {
    for (int i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++)
        vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
    uint16_t blank = (uint16_t)(' ') | ((uint16_t)vga_color << 8);
    for (int i = VGA_WIDTH * (VGA_HEIGHT - 1); i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga_buffer[i] = blank;
    vga_row = VGA_HEIGHT - 1;
}

void vga_putc(char c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\t') {
        vga_col = (vga_col + 8) & ~7;
    } else {
        vga_buffer[vga_row * VGA_WIDTH + vga_col] =
            (uint16_t)c | ((uint16_t)vga_color << 8);
        vga_col++;
    }
    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
    }
    if (vga_row >= VGA_HEIGHT)
        vga_scroll();
}

void vga_puts(const char *s) {
    while (*s)
        vga_putc(*s++);
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_color = (bg << 4) | (fg & 0x0F);
}

void vga_put_dec(uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) { vga_putc('0'); return; }
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (--i >= 0) vga_putc(buf[i]);
}

void vga_put_hex(uint32_t val) {
    const char hex[] = "0123456789abcdef";
    vga_puts("0x");
    for (int i = 28; i >= 0; i -= 4)
        vga_putc(hex[(val >> i) & 0xF]);
}
