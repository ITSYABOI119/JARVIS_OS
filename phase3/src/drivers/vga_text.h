#ifndef VGA_TEXT_H
#define VGA_TEXT_H

#include <stdint.h>

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_ADDR    0xB8000

#define VGA_BLACK       0x0
#define VGA_LIGHT_GREY  0x7
#define VGA_WHITE       0xF
#define VGA_LIGHT_GREEN 0xA
#define VGA_LIGHT_RED   0xC
#define VGA_LIGHT_CYAN  0xB
#define VGA_YELLOW      0xE

#define VGA_DEFAULT_COLOR ((VGA_BLACK << 4) | VGA_LIGHT_GREY)

void vga_init(void);
void vga_clear(void);
void vga_putc(char c);
void vga_puts(const char *s);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_put_dec(uint32_t val);
void vga_put_hex(uint32_t val);

#endif
