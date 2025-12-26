# Phase 2 Serial Logs

This directory stores serial console logs from Raspberry Pi 4 via PuTTY.

## PuTTY Configuration

**Session Settings:**
- Connection type: Serial
- Serial line: COM# (check Device Manager when USB-serial adapter connected)
- Speed: 115200

**Serial Settings:**
- Data bits: 8
- Stop bits: 1
- Parity: None
- Flow control: None

**Logging Settings:**
- Session logging: All session output
- Log file: `C:\Users\jluca\Documents\JARVIS_OS\phase2\logs\putty.log`
- Options: Always append to end of file

## Expected Boot Output (seL4 on Pi 4)

```
ELF-loader started on CPU: ARM Ltd. Cortex-A72 r0p3
  paddr=[80000,...]
  vaddr=[...]
Bringing up 3 other cpus
Switching to EL1
seL4 Kernel booting...
[JARVIS] Decision cache initialized: 258 patterns
[JARVIS] IPC handler ready (polling mode)
[JARVIS] Waiting for Python AI agent...
```

## Log Files

| File | Description |
|------|-------------|
| `putty.log` | Main serial log (append mode) |
| `boot_YYYYMMDD_HHMMSS.log` | Individual boot sessions (optional) |

## Troubleshooting

**No output:**
- Check GPIO wiring (TX→RXD, RX→TXD, GND→GND)
- Verify baud rate matches (115200)
- Ensure `enable_uart=1` in config.txt

**Garbage characters:**
- Baud rate mismatch - both ends must be 115200
- Check USB-serial adapter driver

**seL4 doesn't start:**
- Verify kernel8.img is correct file
- Check start4.elf and fixup4.dat are present
- Review config.txt settings
