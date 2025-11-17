/*
 * JARVIS AI-OS - stdin Implementation for seL4
 * Week 4: stdin support via serial port polling
 *
 * libsel4muslcsys doesn't implement sys_readv for stdin.
 * This provides a simple polling-based implementation.
 *
 * LIMITATION: Uses busy-wait polling (not interrupt-driven)
 * This is acceptable for Phase 1 proof-of-concept.
 */

#include <stdint.h>
#include <stddef.h>
#include <sel4/sel4.h>
#include <sys/uio.h>

/*
 * Serial port I/O (x86 PC COM1 = 0x3F8)
 * This is the standard PC serial port used by seL4 for debug output
 */
#define SERIAL_PORT_BASE 0x3F8
#define SERIAL_DATA      (SERIAL_PORT_BASE + 0)  /* Data register */
#define SERIAL_LSR       (SERIAL_PORT_BASE + 5)  /* Line Status Register */
#define SERIAL_LSR_DR    0x01                     /* Data Ready bit */

/* Inline assembly for x86 port I/O */
static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/*
 * Check if serial input is available
 * Returns: 1 if data ready, 0 otherwise
 */
static int serial_readable(void)
{
    return (inb(SERIAL_LSR) & SERIAL_LSR_DR) != 0;
}

/*
 * Read one character from serial port (non-blocking)
 * Returns: character if available, -1 otherwise
 */
static int serial_getchar_nonblock(void)
{
    if (!serial_readable()) {
        return -1;
    }
    return inb(SERIAL_DATA);
}

/*
 * serial_getchar - Direct serial input (blocking)
 *
 * Polls serial port and returns one character.
 * This bypasses stdio to avoid conflicts with libsel4muslcsys.
 *
 * Returns: character code (0-255), or -1 on error
 */
int serial_getchar(void)
{
    /* Poll until data available */
    while (!serial_readable()) {
        seL4_Yield();
    }
    return inb(SERIAL_DATA);
}

/*
 * serial_getchar_timeout - Get char with timeout
 *
 * @param max_yields Maximum number of yields before timeout
 * Returns: character code, or -1 on timeout
 */
int serial_getchar_timeout(int max_yields)
{
    int yields = 0;
    while (!serial_readable()) {
        if (yields++ >= max_yields) {
            return -1;  /* Timeout */
        }
        seL4_Yield();
    }
    return inb(SERIAL_DATA);
}
