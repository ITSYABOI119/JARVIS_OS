/*
 * JARVIS AI-OS - stdin Implementation Header
 * Week 4: Serial input functions
 */

#ifndef STDIN_IMPL_H
#define STDIN_IMPL_H

/*
 * Get one character from serial port (blocking)
 * Returns: character code (0-255)
 */
int serial_getchar(void);

/*
 * Get one character with timeout
 * @param max_yields Maximum yields before timeout
 * Returns: character code, or -1 on timeout
 */
int serial_getchar_timeout(int max_yields);

#endif /* STDIN_IMPL_H */
