# Phase 2 Serial Console Logs

This directory contains serial console logs from Pi 4 boot tests.

## Files

- `serial_console.log` - Complete serial output from all boot sessions (appended)
- `uart_bench_500.csv` - 500-query UART IPC bench results (CSV)
- `python_uart_bench_500.log` - UART IPC RX/TX hexdump log (text)
- `python_uart_bench_500.bin` - Raw UART RX byte stream (binary)

## What Gets Logged

Every time you boot the Pi 4 with the serial console connected, it captures:
- ELF-loader startup messages
- seL4 kernel boot
- JARVIS banner and system info
- Decision cache initialization
- UART IPC handler startup
- Any errors or warnings

UART IPC bench runs add:
- Per-query RTT and status (CSV)
- Raw RX/TX hexdumps (text)
- Raw RX bytes (binary)

## Log Format

The logger adds a header for each session:
```
=~=~=~=~=~=~=~=~=~=~=~= serial console log 2026.01.02 09:50:33 =~=~=~=~=~=~=~=~=~=~=~=
```

Followed by all serial output until you close the session.

## Usage

**View recent boot:**
```cmd
# Last 50 lines
type serial_console.log | more

# Search for errors
findstr /I "error fail panic" serial_console.log
```

**Clear old logs:**
```cmd
# Backup first
copy serial_console.log serial_console.log.backup

# Clear
del serial_console.log
```

## Configured in Serial Console App

See `docs/SERIAL_CONSOLE_SETUP.md` for logging configuration:
- Session -> Logging
- All session output
- Always append
- Flush frequently
