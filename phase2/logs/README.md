# Phase 2 Serial Console Logs

This directory contains PuTTY serial console logs from Pi 4 boot tests.

## Files

- `putty.log` - Complete serial output from all boot sessions (appended)

## What Gets Logged

Every time you boot the Pi 4 with PuTTY connected, it captures:
- ELF-loader startup messages
- seL4 kernel boot
- JARVIS banner and system info
- Decision cache initialization
- UART IPC handler startup
- Any errors or warnings

## Log Format

PuTTY adds a header for each session:
```
=~=~=~=~=~=~=~=~=~=~=~= PuTTY log 2026.01.02 09:50:33 =~=~=~=~=~=~=~=~=~=~=~=
```

Followed by all serial output until you close the session.

## Usage

**View recent boot:**
```cmd
# Last 50 lines
type putty.log | more

# Search for errors
findstr /I "error fail panic" putty.log
```

**Clear old logs:**
```cmd
# Backup first
copy putty.log putty.log.backup

# Clear
del putty.log
```

## Configured in PuTTY

See `docs/PUTTY_SETUP.md` for PuTTY logging configuration:
- Session → Logging
- All session output
- Always append
- Flush frequently
