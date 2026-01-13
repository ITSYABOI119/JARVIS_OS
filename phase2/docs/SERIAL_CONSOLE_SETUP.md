# Serial Console Configuration for JARVIS Pi 4

**Last Updated:** January 2, 2026

## Your Working Configuration

### Session Settings
- **Connection type:** Serial
- **Serial line:** COM3
- **Speed:** 115200
- **Saved session name:** Pi4-UART

### Logging Settings
- **Session logging:** All session output
- **Log file name:** `C:\Users\jluca\Documents\JARVIS_OS\phase2\logs\serial_console.log`
- **If log exists:** Always append to end of it
- **Flush log frequently:** ✓ Enabled
- **Include header:** ✓ Enabled

### Serial Port Settings (Connection → Serial) - CRITICAL
- **Serial line:** COM3
- **Speed (baud):** 115200
- **Data bits:** 8
- **Stop bits:** 1
- **Parity:** None
- **Flow control:** None

**This is the 8N1 configuration** (8 data bits, No parity, 1 stop bit)
**Flow control MUST be None** for bare-metal embedded systems

### Terminal Settings (Connection → Data)
- **Terminal-type string:** xterm
- **Terminal speeds:** 38400,38400
- **Auto-login username:** (empty - not needed for serial)
- **Environment variables:** (none)

### How to Connect

1. **Find COM Port:**
   - Open Device Manager → Ports (COM & LPT)
   - Look for "USB Serial Port (COM3)" or similar
   - Note the COM number

2. **serial console Session Tab:**
   - Connection type: ☑ Serial
   - Serial line: `COM3` (or your adapter's port)
   - Speed: `115200`

3. **serial console Connection → Serial Settings:**
   - Serial line: `COM3` (same as session)
   - Speed (baud): `115200`
   - Data bits: `8`
   - Stop bits: `1`
   - Parity: `None`
   - Flow control: `None` ⚠️ **Important: Must be None, not XON/XOFF**

4. **Save the Session:**
   - In "Saved Sessions" field, type: `Pi4-UART`
   - Click **Save**
   - Next time, just select it and click **Load**

5. **Click Open** to connect

## Expected Output

When Pi 4 boots, you should see:

```
ELF-loader started on CPU 0
  paddr=[0x0000000000080000..0x00000000017f0fff]

========================================
  JARVIS AI-OS v0.2 - Phase 2 Week 32
  ARM64 Raspberry Pi 4 Port
========================================
  seL4 + PL011 UART + Decision Cache
  Build: Jan  1 2026 XX:XX:XX
========================================

System Information:
  Architecture: aarch64 (ARM64)
  Platform: Raspberry Pi 4 (BCM2711)
  CPU: Cortex-A72 @ 1.8 GHz
  Kernel: seL4 microkernel
  UART: PL011 @ 0xFE201000
  Baud: 115200 8N1
  Phase: 2 Week 32

Initializing decision cache...
  Cache initialized (512 entries)
  Loaded 258 patterns into cache

Cache Stats: 258 entries loaded

Starting UART IPC handler...

========================================
UART IPC Handler Running (ARM64)
Waiting for Python queries...
========================================
```

## Troubleshooting

### No Output
1. Check wiring (TXD↔RXD must be crossed)
2. Verify COM port in Device Manager
3. Try different USB port (prefer USB 2.0 / black ports)
4. Check Pi 4 is powered on (red LED lit)

### Garbage Characters
1. Verify baud rate is exactly 115200
2. Check Serial settings are 8N1 (8 data, None parity, 1 stop)
3. Try shorter USB cable (reduces electrical noise)

### Wrong COM Port
If serial console can't open COM3:
1. Check Device Manager for actual COM#
2. Update serial console "Serial line" to match
3. Close any other programs using the serial port

## Hardware Connections

```
Pi 4 GPIO Header          USB-Serial Adapter
─────────────────         ──────────────────
Pin 6  (GND)        ───── GND
Pin 8  (GPIO14/TXD) ───── RXD  (Pi transmits → Adapter receives)
Pin 10 (GPIO15/RXD) ───── TXD  (Adapter transmits → Pi receives)

Power: USB-C to Pi 4 (separate from serial adapter)
```

**CRITICAL:** TXD and RXD must be **crossed** (Pi TX → Adapter RX)

## Session Management

**Save current settings:**
1. Configure all settings
2. Type session name in "Saved Sessions"
3. Click **Save**

**Load saved session:**
1. Select session from list (e.g., "Pi4-UART")
2. Click **Load**
3. Click **Open**

**Delete old session:**
1. Select session from list
2. Click **Delete**

## Alternative: Windows Terminal

If you prefer Windows Terminal instead of serial console:

```powershell
# Install if not present
winget install Microsoft.WindowsTerminal

# Connect via mode command first
mode COM3 BAUD=115200 PARITY=N DATA=8 STOP=1

# Then use PowerShell to read
$port = new-Object System.IO.Ports.SerialPort COM3,115200,None,8,one
$port.Open()
while($true) {
    if ($port.BytesToRead) {
        $port.ReadExisting()
    }
}
```

## Logging Configuration

**Why enable logging:**
- Captures complete boot sequence for debugging
- Preserves error messages that scroll off screen
- Allows sharing boot output for troubleshooting
- Creates permanent record of JARVIS boot process

**serial console Logging Setup (Session → Logging):**

1. **Session logging:** Select "All session output"
2. **Log file name:** Click Browse and set to:
   ```
   C:\Users\jluca\Documents\JARVIS_OS\phase2\logs\serial_console.log
   ```
3. **If log exists:** Select "Always append to end of it"
4. **Options:**
   - ✓ Flush log file frequently (writes immediately, safer)
   - ✓ Include header (shows timestamp of each session)

**Log File Location:**
```
C:\Users\jluca\Documents\JARVIS_OS\phase2\logs\serial_console.log
```

This will capture:
- ELF-loader startup messages
- JARVIS banner
- Decision cache initialization
- All UART IPC messages
- Any errors or warnings

**Viewing the log:**
```cmd
# View last 50 lines
type C:\Users\jluca\Documents\JARVIS_OS\phase2\logs\serial_console.log | more

# Or open in notepad
notepad C:\Users\jluca\Documents\JARVIS_OS\phase2\logs\serial_console.log
```

## Screenshot Reference

**Session Configuration:**
![serial console Session Configuration](../../Pictures/Screenshots/Screenshot%202026-01-02%20095024.png)
- Serial line: COM3
- Speed: 115200
- Connection type: Serial
- Saved as: Pi4-UART

**Logging Configuration:**
![serial console Logging Configuration](../../Pictures/Screenshots/Screenshot%202026-01-02%20095033.png)
- Session logging: All session output
- Log file: phase2/logs/serial_console.log
- Always append to end
- Flush frequently enabled

**Serial Port Settings (Connection → Serial):**
![serial console Serial Configuration](../../Pictures/Screenshots/Screenshot%202026-01-02%20095712.png)
- Serial line: COM3
- Speed: 115200
- Data bits: 8, Stop bits: 1, Parity: None (8N1 config)
- Flow control: **None** (⚠️ Screenshot shows XON/XOFF - this was corrected to None)

**Terminal Settings (Connection → Data):**
![serial console Data Configuration](../../Pictures/Screenshots/Screenshot%202026-01-02%20095212.png)
- Terminal-type: xterm
- Terminal speeds: 38400,38400 (not used for serial, only SSH/Telnet)
- Auto-login: empty (serial doesn't use login)
