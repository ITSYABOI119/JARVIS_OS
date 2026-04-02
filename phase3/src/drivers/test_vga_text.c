#include <stdio.h>
#include <stdint.h>

/* Mock VGA framebuffer — vga_text.c references this when JARVIS_TEST_MOCK is defined */
uint16_t jarvis_test_vga_buffer[80 * 25];

#include "vga_text.h"

static int pass = 0, fail = 0;

#define EXPECT(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fail++; } \
    else { printf("PASS: %s\n", msg); pass++; } \
} while(0)

/* ---- Tests ---- */

static void test_init_clears_buffer(void) {
    vga_init();
    int ok = 1;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        uint16_t expected = (uint16_t)(' ') | ((uint16_t)VGA_DEFAULT_COLOR << 8);
        if (jarvis_test_vga_buffer[i] != expected) { ok = 0; break; }
    }
    EXPECT(ok, "init clears buffer to space+default color");
}

static void test_putc_writes_char(void) {
    vga_init();
    vga_putc('A');
    uint16_t expected = (uint16_t)'A' | ((uint16_t)VGA_DEFAULT_COLOR << 8);
    EXPECT(jarvis_test_vga_buffer[0] == expected, "putc('A') writes 0x0741 at [0,0]");
}

static void test_newline_advances_row(void) {
    vga_init();
    vga_putc('X');
    vga_putc('\n');
    vga_putc('Y');
    /* 'Y' should be at row 1, col 0 → buffer index 80 */
    uint16_t expected = (uint16_t)'Y' | ((uint16_t)VGA_DEFAULT_COLOR << 8);
    EXPECT(jarvis_test_vga_buffer[VGA_WIDTH] == expected, "newline advances to row 1 col 0");
}

static void test_column_wrap(void) {
    vga_init();
    for (int i = 0; i < 80; i++)
        vga_putc('.');
    /* After 80 chars, cursor should be at row 1, col 0 */
    vga_putc('W');
    uint16_t expected = (uint16_t)'W' | ((uint16_t)VGA_DEFAULT_COLOR << 8);
    EXPECT(jarvis_test_vga_buffer[VGA_WIDTH] == expected, "column wrap: 81st char at row 1 col 0");
}

static void test_scroll(void) {
    vga_init();
    /* Put 'B' across row 1 */
    vga_putc('\n');
    for (int i = 0; i < VGA_WIDTH; i++)
        vga_putc('B');
    /* Cursor is now at row 2, col 0 (wrapped after 80 chars).
       We need exactly one scroll: advance cursor to row VGA_HEIGHT.
       That requires VGA_HEIGHT - 2 newlines (from row 2 to row 25). */
    for (int r = 2; r < VGA_HEIGHT; r++)
        vga_putc('\n');
    /* Cursor hit row 25 on the last newline, triggering one scroll.
       Row 1 ('B's) should now be at row 0. */
    uint16_t expected = (uint16_t)'B' | ((uint16_t)VGA_DEFAULT_COLOR << 8);
    EXPECT(jarvis_test_vga_buffer[0] == expected, "scroll moves row 1 to row 0");
}

static void test_set_color(void) {
    vga_init();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_putc('G');
    uint16_t expected = (uint16_t)'G' | ((uint16_t)0x0A << 8);
    EXPECT(jarvis_test_vga_buffer[0] == expected, "set_color(GREEN,BLACK) uses 0x0A attribute");
}

static void test_put_dec(void) {
    vga_init();
    vga_put_dec(42);
    uint16_t exp4 = (uint16_t)'4' | ((uint16_t)VGA_DEFAULT_COLOR << 8);
    uint16_t exp2 = (uint16_t)'2' | ((uint16_t)VGA_DEFAULT_COLOR << 8);
    int ok = (jarvis_test_vga_buffer[0] == exp4) && (jarvis_test_vga_buffer[1] == exp2);
    EXPECT(ok, "put_dec(42) writes '4' then '2'");
}

/* ---- Main ---- */

int main(void) {
    printf("=== VGA Text Mode Driver Tests ===\n");
    test_init_clears_buffer();
    test_putc_writes_char();
    test_newline_advances_row();
    test_column_wrap();
    test_scroll();
    test_set_color();
    test_put_dec();
    printf("\nResults: %d PASS, %d FAIL (of %d)\n", pass, fail, pass + fail);
    return fail > 0 ? 1 : 0;
}
