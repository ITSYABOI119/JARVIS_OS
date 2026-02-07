# JARVIS AI-OS User Guide

**Version:** 0.2 Alpha - Phase 2
**Platform:** Raspberry Pi 4 (BCM2711) + Host PC
**Last Updated:** February 2026

---

## Table of Contents

1. [System Overview](#system-overview)
2. [Hardware Requirements](#hardware-requirements)
3. [Software Requirements](#software-requirements)
4. [SD Card Setup](#sd-card-setup)
5. [Hardware Wiring](#hardware-wiring)
6. [First Boot Walkthrough](#first-boot-walkthrough)
7. [Connecting the Python AI Client](#connecting-the-python-ai-client)
8. [Command Reference](#command-reference)
9. [UART IPC Protocol Overview](#uart-ipc-protocol-overview)
10. [USB Keyboard Input](#usb-keyboard-input)
11. [Networking](#networking)
12. [Decision Cache](#decision-cache)
13. [Troubleshooting](#troubleshooting)
14. [Known Limitations](#known-limitations)
15. [Architecture Reference](#architecture-reference)

---

## System Overview

JARVIS AI-OS is an AI-controlled operating system built on the seL4 formally verified
microkernel. The system uses a split architecture where time-critical operations run on
bare metal (Pi 4) and AI inference runs on a host PC.

### Why Split Architecture?

AI inference takes 50-500ms per decision. Microkernel interrupt handling must respond
in under 1ms. These are 3 orders of magnitude apart. Running AI at Ring 0 is not
feasible. Instead:

- **Pi 4 (seL4):** Runs the microkernel, device drivers, decision cache, and IPC
  handler. Handles interrupts in under 1ms. All code is C.
- **Host PC:** Runs Python AI models (Phi-3 Mini 3.8B, Llama 3.2 1B), the SHIELD
  safety framework, and specialist agents. Communicates with the Pi 4 over UART.

### How It Works

```
+-----------------------+    UART 115200    +-----------------------+
|   Host PC             |<----------------->|   Raspberry Pi 4      |
|                       |    7ms RTT        |                       |
|  Python AI Engine     |                   |  seL4 Microkernel     |
|  - Phi-3 Mini 3.8B   |                   |  Decision Cache       |
|  - Llama 3.2 1B      |                   |  - 258 patterns       |
|  - SHIELD Safety      |                   |  - 85.7% hit rate     |
|  - Specialist Agents  |                   |  - <1ms lookup        |
+-----------------------+                   +-----------------------+
```

- **Cache hits (85.7%):** The Pi 4 decision cache answers queries in under 1ms
  without contacting the host.
- **Cache misses (14.3%):** Queries are forwarded to the host PC via UART (7ms RTT).

---

## Hardware Requirements

### Required

| Component | Specification | Notes |
|-----------|--------------|-------|
| Raspberry Pi 4 | Model B, 8GB RAM recommended | BCM2711, Cortex-A72 quad-core |
| microSD Card | 16GB+ Class 10 or better | FAT32 boot partition required |
| USB-to-Serial Adapter | CP2102, CH340, or FTDI | Must support 3.3V TTL |
| Jumper Wires | Female-to-female, 3 needed | For UART: TX, RX, GND |
| Power Supply | USB-C, 5V 3A | Official Pi 4 PSU recommended |
| Host PC | Linux, Windows, or macOS | Python 3.8+ required |

### Optional

| Component | Specification | Notes |
|-----------|--------------|-------|
| Ethernet Cable | Cat5e or better | For networking commands (ping, ifconfig) |
| USB-C OTG Adapter | USB-C male to USB-A female | For USB keyboard via USB-C port |
| USB Keyboard | Any standard USB keyboard | Boot protocol keyboards supported |
| microSD Card Reader | USB or built-in | For flashing the SD card |

### Important Hardware Notes

- The Pi 4 USB-A ports use the VL805 xHCI controller which is NOT supported.
  USB keyboards must connect via the USB-C port using an OTG adapter.
- The UART connection uses 3.3V TTL logic levels. Do NOT use RS-232 adapters
  (they use +/-12V and will damage the Pi).
- Do NOT connect the 5V/VCC pin from the USB-serial adapter to the Pi. The Pi
  must be powered separately via its USB-C power port.

---

## Software Requirements

### Host PC

| Software | Version | Purpose |
|----------|---------|---------|
| Python | 3.8 or later | AI client, test scripts |
| pyserial | Any recent version | Serial port communication |
| Git | Any recent version | Cloning the repository |

Optional (for AI models):
| Software | Version | Purpose |
|----------|---------|---------|
| CUDA Toolkit | 11.8+ | GPU-accelerated inference |
| ollama | Latest | Running Llama 3.2 1B locally |

Install pyserial:
```bash
pip install pyserial
```

### Build Environment (for developers only)

Building the kernel image from source requires:

| Software | Version | Purpose |
|----------|---------|---------|
| WSL2 or Linux | Ubuntu 20.04+ | Build environment |
| aarch64-linux-gnu-gcc | 13.3.0 | ARM64 cross-compiler |
| CMake | 3.16+ | Build system |
| Ninja | 1.10+ | Build backend |
| seL4 Build System | TII fork | Microkernel build |

Most users do not need to build from source. Pre-built `kernel8.img` files are
provided in `phase2/firmware/`.

---

## SD Card Setup

### Boot Partition Contents

The SD card must have a FAT32 boot partition with these files:

```
SD Card (FAT32):
+-- start4.elf          GPU firmware (from Raspberry Pi firmware repo)
+-- fixup4.dat          Memory configuration (from Raspberry Pi firmware repo)
+-- bcm2711-rpi-4-b.dtb Device tree blob (from Raspberry Pi firmware repo)
+-- u-boot.bin          U-Boot 2026.01 bootloader
+-- boot.scr            U-Boot boot script
+-- config.txt          Boot configuration
+-- kernel8.img         JARVIS seL4 kernel image (~1.7MB)
```

### config.txt

Create `config.txt` with exactly this content:

```
arm_64bit=1
enable_uart=1
dtoverlay=disable-bt
uart_2ndstage=1
boot_delay=1
init_uart_clock=48000000
gpio=15=pu

kernel=u-boot.bin
```

Key settings:
- `arm_64bit=1` -- Boots in 64-bit mode (required for aarch64 seL4)
- `enable_uart=1` -- Enables PL011 UART on GPIO 14/15
- `dtoverlay=disable-bt` -- Routes PL011 to GPIO pins (instead of mini UART)
- `kernel=u-boot.bin` -- GPU loads U-Boot first, which then loads kernel8.img

### Manual SD Card Setup (Windows)

```cmd
:: Format SD card as FAT32 (use Disk Management or diskpart)
:: Copy all boot files to the SD card root
copy phase2\firmware\start4.elf D:\
copy phase2\firmware\fixup4.dat D:\
copy phase2\firmware\bcm2711-rpi-4-b.dtb D:\
copy phase2\firmware\u-boot.bin D:\
copy phase2\firmware\boot.scr D:\
copy phase2\firmware\config.txt D:\
copy phase2\firmware\kernel8.img D:\
```

### Manual SD Card Setup (Linux)

```bash
# Mount the SD card
sudo mount /dev/sdX1 /mnt/sd

# Copy all boot files
sudo cp phase2/firmware/start4.elf /mnt/sd/
sudo cp phase2/firmware/fixup4.dat /mnt/sd/
sudo cp phase2/firmware/bcm2711-rpi-4-b.dtb /mnt/sd/
sudo cp phase2/firmware/u-boot.bin /mnt/sd/
sudo cp phase2/firmware/boot.scr /mnt/sd/
sudo cp phase2/firmware/config.txt /mnt/sd/
sudo cp phase2/firmware/kernel8.img /mnt/sd/

# Unmount
sudo umount /mnt/sd
```

### Updating the Kernel

To update just the kernel image (e.g., after a new build):

```cmd
:: Windows
copy phase2\firmware\kernel8.img D:\
```

```bash
# Linux
sudo cp phase2/firmware/kernel8.img /mnt/sd/
```

---

## Hardware Wiring

### UART Connection

Connect the USB-serial adapter to the Pi 4 GPIO header:

```
Raspberry Pi 4 GPIO Header          USB-Serial Adapter
+-----------------------------+    +-----------------------------+
|  Pin 8  (GPIO14/TXD) -----------> RXD                         |
|  Pin 10 (GPIO15/RXD) <----------- TXD                         |
|  Pin 6  (GND)        -----------> GND                         |
+-----------------------------+    +-----------------------------+
                                              |
                                              v
                                   Host Computer USB Port
```

**Pin numbering reference (Pi 4 GPIO header, board numbering):**

```
       3V3  (1) (2)  5V
     GPIO2  (3) (4)  5V
     GPIO3  (5) (6)  GND  <--- Connect GND here
            (7) (8)  GPIO14 (TXD) <--- Pi TX -> Adapter RX
       GND  (9) (10) GPIO15 (RXD) <--- Pi RX <- Adapter TX
```

**Critical wiring rules:**

1. Pi TX (Pin 8) connects to adapter RX
2. Pi RX (Pin 10) connects to adapter TX
3. GND (Pin 6 or Pin 9) connects to adapter GND
4. Do NOT connect 5V or 3.3V power between the adapter and Pi

### Ethernet Connection (Optional)

For networking commands, connect an Ethernet cable to the Pi 4 RJ45 jack.
The Pi 4 uses the BCM54213PE Gigabit Ethernet PHY via the GENET v5 controller.

Static IP configuration (hardcoded):
- IP: 10.0.0.2
- Netmask: 255.255.255.0 (/24)
- Gateway: 10.0.0.1

Your network equipment or directly-connected host must be on the 10.0.0.0/24 subnet.

### USB Keyboard (Optional)

Connect a USB keyboard to the Pi 4 USB-C port using a USB-C OTG adapter:

```
USB Keyboard --> USB-A to USB-C OTG Adapter --> Pi 4 USB-C Port
```

The Pi 4's USB-A ports are connected to the VL805 xHCI controller, which is
not supported. Only the USB-C port (DWC2 OTG controller) works.

Boot protocol keyboards are supported. Most standard USB keyboards work.

---

## First Boot Walkthrough

### Step 1: Prepare Hardware

1. Insert the prepared SD card into the Pi 4
2. Connect the USB-serial adapter between the Pi and host PC
3. (Optional) Connect Ethernet cable
4. (Optional) Connect USB keyboard via USB-C OTG adapter

### Step 2: Open Serial Terminal

Open a serial terminal on your host PC at 115200 baud, 8N1:

**Linux:**
```bash
# Find the serial port
ls /dev/ttyUSB* /dev/ttyACM*

# Open with screen, minicom, or picocom
screen /dev/ttyUSB0 115200
# or
minicom -b 115200 -D /dev/ttyUSB0
# or
picocom -b 115200 /dev/ttyUSB0
```

**Windows:**
- Use PuTTY, TeraTerm, or the Arduino Serial Monitor
- Select the correct COM port (check Device Manager)
- Set baud rate to 115200, 8 data bits, no parity, 1 stop bit

**macOS:**
```bash
ls /dev/tty.usbserial* /dev/tty.usbmodem*
screen /dev/tty.usbserial-XXXX 115200
```

### Step 3: Power On

Connect the USB-C power supply to the Pi 4. You will see output on the serial
terminal in this order:

#### 1. U-Boot Bootloader

```
U-Boot 2026.01 (Jan 20 2026 - 14:30:22 +0000)

DRAM:  8 GiB
...
Hit any key to stop autoboot:  3
```

U-Boot has a 3-second countdown. If you do nothing, it auto-boots. Press any
key during the countdown to enter the U-Boot interactive shell (useful for
debugging).

#### 2. seL4 Kernel Boot

```
Bootstrapping kernel
Booting all finished, dropped to user space
```

This appears almost immediately after U-Boot loads kernel8.img.

#### 3. JARVIS Banner and Initialization

```
========================================
  JARVIS AI-OS v0.2 - Phase 2 Week 33
  ARM64 Raspberry Pi 4 Port
========================================
  seL4 + PL011 UART + Decision Cache
  Build: Feb 07 2026 15:30:00
========================================
```

#### 4. Driver Initialization

```
[INIT] System timer...
[INIT] UART PL011 device frame...
[INIT] Slot allocator...
[INIT] DMA allocator...
[INIT] EMMC/SDHCI...
[INIT] GENET Ethernet...
[INIT] USB HID keyboard...
```

#### 5. Test Suite

The built-in test suite runs automatically on every boot. Expected output:

```
EMMC TEST SUITE
========================================
[EMMC][TEST] mmio_map=PASS
[EMMC][TEST] init=PASS
[EMMC][TEST] read_lba0=PASS
[EMMC][TEST] mbr_sig=PASS
[EMMC][TEST] part0_lba=PASS
[EMMC][TEST] bpb_sector_size=PASS
[EMMC][TEST] bpb_sig=PASS
[EMMC][TEST] multi_read=PASS
[EMMC][TEST] throughput=PASS
[EMMC][TEST] null_ptr_check=PASS
[EMMC][TEST] write_single=PASS
[EMMC][TEST] write_verify=PASS
[EMMC] TEST RESULTS: 12 PASS, 0 FAIL

GENET ETHERNET (Week 37)
========================================
[GENET][TEST] init=PASS
[GENET][TEST] read_rev=PASS
[GENET][TEST] set_mac=PASS
[GENET][TEST] phy_init=PASS
[GENET][TEST] tx_ring_init=PASS
[GENET][TEST] tx_send_arp=PASS
[GENET] TEST RESULTS: 6 PASS, 0 FAIL

RX + NETWORKING (Week 38)
========================================
[W38][TEST] rx_ring_init=PASS
[W38][TEST] rx_poll=PASS
[W38][TEST] arp_reply=PASS
[W38][TEST] icmp_reply=PASS
[W38][TEST] rx_bounds=PASS
[W38] TEST RESULTS: 5 PASS, 0 FAIL

SHELL CMD + GENET INTEGRATION (Week 39)
========================================
[W39][TEST] link_status=PASS
[W39][TEST] link_speed=PASS
[W39][TEST] stats_counter=PASS
[W39][TEST] arp_cache_hit=PASS
[W39][TEST] arp_cache_miss=PASS
[W39][TEST] arp_request_build=PASS
[W39][TEST] icmp_request_build=PASS
[W39][TEST] cmd_ifconfig=PASS
[W39][TEST] cmd_netstat=PASS
[W39][TEST] cmd_ping_gateway=PASS (or SKIP if no link)
[W39] TEST RESULTS: 9 PASS, 0 FAIL, 1 SKIP

USB HID DRIVER (Week 40)
========================================
[W40] Test 1: dwc2_reg_access... PASS
[W40] Test 2: dwc2_core_init... PASS
[W40] Test 3: port_status... PASS
[W40] Test 4: usb_enumeration... SKIP (no device)
[W40] Test 5: scancode_table... PASS
[W40] Test 6: hid_poll... SKIP (no keyboard)
[W40] TEST RESULTS: 4 PASS, 0 FAIL, 2 SKIP

USB HID KEYBOARD TESTS (Week 41)
========================================
[W41] Test 1: ctrl_c... PASS
[W41] Test 2: ctrl_d... PASS
[W41] Test 3: ctrl_a... PASS
[W41] Test 4: caps_lock_toggle... PASS
[W41] Test 5: caps_lock_letter... PASS
[W41] Test 6: special_key_arrow... PASS
[W41] Test 7: debounce_held_key... PASS
[W41] Test 8: debounce_new_key... PASS
[W41] Test 9: usb_status_cmd... PASS
[W41] Test 10: line_buf_overflow... PASS
[W41] TEST RESULTS: 10 PASS, 0 FAIL, 0 SKIP
```

**Total expected: 46 PASS, 0 FAIL, 3 SKIP**

The 3 SKIPs are normal when:
- No USB keyboard is attached (2 tests skip)
- No Ethernet cable is connected (1 test skips)

#### 6. IPC Handler Ready

```
========================================
UART IPC Handler Running (ARM64)
Waiting for Python queries...
========================================
```

At this point, the system is ready to accept commands from the Python AI client
or from a USB keyboard.

---

## Connecting the Python AI Client

The Python AI client runs on the host PC and communicates with the Pi 4 over UART.

### Basic Connection

```python
from phase2.src.ai.uart_ipc_client import UARTIPCClient

# Connect to the Pi 4
client = UARTIPCClient('/dev/ttyUSB0')  # Linux
# client = UARTIPCClient('COM3')        # Windows
client.connect()

# Send a cache lookup query
result = client.cache_lookup("list files")
print(result)
# {'status': 0, 'hit': True, 'action': 'exec_ls', 'trust': 0}

# Send a shell command
result = client.send_command("ifconfig")
print(result)

# Disconnect
client.disconnect()
```

### Serial Port Names

| OS | Typical Port Name |
|----|-------------------|
| Linux | `/dev/ttyUSB0` or `/dev/ttyACM0` |
| Windows | `COM3`, `COM4`, etc. (check Device Manager) |
| macOS | `/dev/tty.usbserial-XXXX` |

### Connection Parameters

The client automatically uses these settings (matching the seL4 UART driver):

| Parameter | Value |
|-----------|-------|
| Baud Rate | 115200 |
| Data Bits | 8 |
| Parity | None |
| Stop Bits | 1 |
| Flow Control | None |
| Heartbeat | Every 5 seconds |
| Timeout | 30 seconds (link considered dead) |

---

## Command Reference

Commands can be sent via the Python client (UART IPC type 0x07 COMMAND) or typed
on a USB keyboard connected to the Pi 4.

### ping

Send ICMP echo requests to a target IP address.

**Syntax:** `ping <ip_address>`

**Example:**
```
ping 10.0.0.1
```

**Output:**
```
Reply from 10.0.0.1: time=0.8ms TTL=64
Reply from 10.0.0.1: time=0.6ms TTL=64
Reply from 10.0.0.1: time=0.7ms TTL=64
Reply from 10.0.0.1: time=0.5ms TTL=64
4/4 received, 0% loss, avg=0ms
```

**Behavior:**
- Sends 4 ICMP echo requests with a 1-second timeout per request
- Performs ARP resolution first (2-second timeout)
- Displays per-packet RTT and TTL
- Displays summary with packet loss and average RTT
- Requires Ethernet link to be UP

**Error conditions:**
- `ping 10.0.0.1: link DOWN` -- Ethernet cable not connected or PHY link not established
- `ping 10.0.0.1: ARP timeout` -- Target did not respond to ARP request within 2 seconds
- `Request timed out` -- ICMP echo request not answered within 1 second
- `Usage: ping <ip>` -- Invalid or missing IP address argument

### ifconfig

Display network interface configuration.

**Syntax:** `ifconfig`

**Output:**
```
eth0: UP
  MAC: c8:xx:xx:xx:xx:xx
  IP:  10.0.0.2
  Link: 1000 Mbps Full
```

**Fields:**
- **eth0:** Interface name and status (UP or DOWN)
- **MAC:** Hardware MAC address (read from Pi 4 OTP or set programmatically)
- **IP:** Static IPv4 address (currently 10.0.0.2)
- **Link:** Negotiated speed and duplex from the BCM54213PE PHY

Possible link speeds: `DOWN`, `10 Mbps Half`, `10 Mbps Full`, `100 Mbps Half`,
`100 Mbps Full`, `1000 Mbps Half`, `1000 Mbps Full`

### netstat

Display network transmission statistics.

**Syntax:** `netstat`

**Output:**
```
TX: 12 pkts, 1024 bytes, 0 err
RX: 8 pkts, 768 bytes, 0 err, 0 drop
ARP cache: 2 entries
```

**Fields:**
- **TX:** Transmitted packets count, total bytes, and error count
- **RX:** Received packets count, total bytes, errors, and dropped packets
- **ARP cache:** Number of active entries in the ARP cache (max 8)

### usb

Display USB/DWC2 controller status.

**Syntax:** `usb`

**Output (no device):**
```
USB DWC2: initialized
  Device: not connected
```

**Output (keyboard connected):**
```
USB DWC2: initialized
  Device: connected (Full Speed)
  Address: 1
  HID EP: IN 0x81, interval=10ms, maxpkt=8
  Caps Lock: OFF
```

**Fields:**
- **USB DWC2:** Controller initialization status
- **Device:** Connection status and negotiated speed (High/Full/Low Speed)
- **Address:** Assigned USB device address
- **HID EP:** Interrupt IN endpoint details (address, polling interval, max packet)
- **Caps Lock:** Current Caps Lock state

### Unknown Commands

If an unrecognized command is entered, the system responds:

```
Unknown command. Try: ping, ifconfig, netstat, usb
```

---

## UART IPC Protocol Overview

The Pi 4 and host PC communicate using a framed binary protocol over UART.

### Frame Format

```
+----------+----------+----------+----------+----------+--------------+----------+
|  SYNC    |  TYPE    |  SEQ     |  LENGTH  |  FLAGS   |  PAYLOAD     |  CRC16   |
|  2 bytes |  1 byte  |  2 bytes |  2 bytes |  1 byte  |  0-240 bytes |  2 bytes |
+----------+----------+----------+----------+----------+--------------+----------+
   0xAA55   See below  Little-E   Little-E   Bit flags   Variable      CRC-CCITT
```

Maximum frame size: 250 bytes (2 + 1 + 2 + 2 + 1 + 240 + 2)

### Message Types

| Value | Name | Direction | Description |
|-------|------|-----------|-------------|
| 0x01 | QUERY | Host -> Pi | Decision cache lookup |
| 0x02 | RESPONSE | Pi -> Host | Cache lookup result |
| 0x03 | HEARTBEAT | Both | Keep-alive (every 5 seconds) |
| 0x04 | HEARTBEAT_ACK | Both | Keep-alive response |
| 0x05 | STATS_REQUEST | Host -> Pi | Request cache statistics |
| 0x06 | STATS_RESPONSE | Pi -> Host | Cache statistics |
| 0x07 | COMMAND | Host -> Pi | Shell command (text string) |
| 0x08 | COMMAND_RESULT | Pi -> Host | Command output (text string) |
| 0x10 | SHIELD_CHECK | Host -> Pi | SHIELD risk assessment |
| 0x11 | SHIELD_RESULT | Pi -> Host | Risk score response |
| 0x20 | STATE_CHANGE | Pi -> Host | System state notification |
| 0x21 | STATE_ACK | Host -> Pi | State change acknowledgment |
| 0xFE | ERROR | Both | Error with code and message |
| 0xFF | RESET | Both | Protocol reset |

### CRC Calculation

CRC-16-CCITT (polynomial 0x1021, initial value 0xFFFF) computed over bytes
from TYPE through end of PAYLOAD (excludes SYNC bytes).

### Timing

| Parameter | Value |
|-----------|-------|
| Heartbeat interval | 5 seconds |
| Link dead timeout | 30 seconds without heartbeat |
| Response timeout | 500 ms per query |
| Retry count | 3 retransmits on timeout |

For full protocol specification, see `phase2/docs/UART_IPC_PROTOCOL.md`.

---

## USB Keyboard Input

When a USB keyboard is connected via USB-C OTG, you can type commands directly
on the Pi 4 without the Python AI client.

### Features

- Character echo on serial console (you see what you type)
- 128-character line buffer
- Backspace support (visual and buffer)
- Enter key dispatches command to the same handler as UART COMMAND messages
- Ctrl+C cancels the current line
- Caps Lock toggle support
- Shift key support for symbols and uppercase
- Arrow keys, F1-F12, Home/End, Page Up/Down, Insert/Delete detected

### Usage

After the system boots and displays "Waiting for Python queries...", simply type
a command on the keyboard and press Enter. The output appears on the serial console.

```
> ifconfig
eth0: UP
  MAC: c8:xx:xx:xx:xx:xx
  IP:  10.0.0.2
  Link: 1000 Mbps Full
> ping 10.0.0.1
Reply from 10.0.0.1: time=0.8ms TTL=64
...
>
```

UART IPC and USB keyboard input work simultaneously. You can use both at the
same time.

---

## Networking

### Static IP Configuration

The networking stack uses a static IP configuration (no DHCP):

| Parameter | Value |
|-----------|-------|
| IP Address | 10.0.0.2 |
| Subnet Mask | 255.255.255.0 (/24) |
| Gateway | 10.0.0.1 |

### Supported Protocols

| Protocol | Support Level |
|----------|---------------|
| Ethernet II | Full (send/receive) |
| ARP | Request and Reply (cache of 8 entries) |
| IPv4 | Basic (no fragmentation) |
| ICMP | Echo Request and Echo Reply |

### Network Stack Architecture

The networking stack is minimal and purpose-built for bare metal:

- **No heap allocation** -- All buffers are passed by the caller
- **No threads** -- Polled receive in the IPC main loop
- **No TCP/UDP** -- Only ARP and ICMP are implemented
- **No DHCP** -- Static IP only
- **No DNS** -- IP addresses must be specified numerically

### ARP Cache

The ARP cache holds up to 8 entries. Entries are populated by:
- Receiving ARP replies
- Processing ARP requests (the sender's IP/MAC is cached)
- Manual addition via `net_arp_add()`

There is no ARP cache timeout or eviction policy. When the cache is full,
new entries overwrite the oldest.

### GENET Ethernet Controller

The BCM2711 GENET v5 Ethernet controller is used for networking:
- PHY: BCM54213PE at MDIO address 1
- Interface: RGMII (Reduced Gigabit Media Independent Interface)
- Speed: Auto-negotiated (typically 1000 Mbps Full Duplex)
- TX ring 16: 256 descriptors
- RX ring 16: 32 descriptors
- DMA buffers: Mapped uncacheable to avoid cache maintenance traps

---

## Decision Cache

The decision cache is a key performance feature that allows the Pi 4 to handle
most AI queries locally without contacting the host PC.

### How It Works

1. Queries are normalized (lowercase, collapsed spaces, trimmed)
2. FNV-1a 64-bit hash is computed
3. Hash is looked up in the pre-compiled pattern table
4. If found (cache hit), the cached action is returned immediately (<1ms)
5. If not found (cache miss), the query is forwarded to the host PC

### Performance

| Metric | Value |
|--------|-------|
| Cache entries | 258 patterns |
| Hit rate | 85.7% |
| Hit latency | <1ms |
| Miss latency | ~7ms (UART round-trip to host) |
| Speedup | 135x (50ms AI inference -> <1ms cache) |

### Query Examples

```python
# These queries hit the cache (fast path):
"list files"       -> ACTION:exec_ls|TRUST:0
"show processes"   -> ACTION:exec_ps|TRUST:0
"what time is it"  -> ACTION:exec_date|TRUST:0

# These queries miss the cache (forwarded to host AI):
"explain quantum computing"  -> forwarded to Phi-3 Mini
```

---

## Troubleshooting

### No Serial Output

**Symptom:** No text appears on the serial terminal after powering on.

**Possible causes and fixes:**

1. **Wrong baud rate:** Ensure your terminal is set to exactly 115200 baud, 8N1.

2. **Wires swapped:** Pi TX (Pin 8) must connect to adapter RX, and Pi RX (Pin 10)
   must connect to adapter TX. Swap the TX/RX wires.

3. **Wrong GPIO pins:** Make sure you are using Pin 8 (GPIO14) and Pin 10 (GPIO15),
   not other GPIO pins.

4. **UART not enabled:** Verify `config.txt` on the SD card contains `enable_uart=1`
   and `dtoverlay=disable-bt`.

5. **Wrong serial port:** On Linux, try `/dev/ttyUSB0`, `/dev/ttyUSB1`, or
   `/dev/ttyACM0`. On Windows, check Device Manager for the correct COM port.

6. **Adapter not 3.3V:** Ensure your USB-serial adapter supports 3.3V TTL.
   5V adapters may not work or may damage the Pi.

### Boot Failure (No U-Boot)

**Symptom:** No U-Boot banner appears. The serial output is empty or shows garbled text.

**Possible causes and fixes:**

1. **Missing files on SD card:** Verify all required files exist: `start4.elf`,
   `fixup4.dat`, `config.txt`, `u-boot.bin`. The GPU cannot boot without its firmware.

2. **Corrupt SD card:** Re-format the SD card as FAT32 and copy files again.

3. **Wrong config.txt:** Ensure `kernel=u-boot.bin` is present. Without it, the GPU
   looks for `kernel8.img` directly (bypassing U-Boot).

4. **Garbled text:** This usually means wrong baud rate. Try 115200.

### Boot Failure (U-Boot Appears but kernel8.img Fails)

**Symptom:** U-Boot banner appears but kernel8.img does not load.

**Possible causes and fixes:**

1. **Missing kernel8.img:** Verify `kernel8.img` exists on the SD card.

2. **Missing boot.scr:** U-Boot uses `boot.scr` to find and load `kernel8.img`.
   Verify it exists on the SD card.

3. **Corrupt kernel image:** Re-copy `kernel8.img` from `phase2/firmware/`.

### seL4 "Insufficient Memory" or "FailedLookup"

**Symptom:** Boot halts with seL4 error messages about memory or capabilities.

This typically means a device mapping failed. The most common cause is building
with incorrect memory layout parameters. Use the pre-built `kernel8.img` from
`phase2/firmware/` instead of building from source.

### GENET Link Down

**Symptom:** `ifconfig` shows `eth0: DOWN` or `Link: DOWN`.

**Possible causes and fixes:**

1. **No Ethernet cable:** Connect an Ethernet cable to the Pi 4 RJ45 jack.

2. **Cable or switch issue:** Try a different cable or switch port.

3. **Link negotiation failure:** The PHY auto-negotiates speed. Wait 2-3 seconds
   after connecting the cable for link to establish.

### Python Client Connection Issues

**Symptom:** The Python client cannot connect or times out.

**Possible causes and fixes:**

1. **Wrong port:** Check the serial port name (see Serial Port Names table above).

2. **Port in use:** Close other serial terminal programs (only one program can
   use a serial port at a time).

3. **Pi not ready:** Wait for the "UART IPC Handler Running" message on the
   serial console before connecting the Python client.

4. **pyserial not installed:** Run `pip install pyserial`.

5. **Permission denied (Linux):** Add your user to the `dialout` group:
   ```bash
   sudo usermod -a -G dialout $USER
   # Log out and back in for the change to take effect
   ```

### USB Keyboard Not Working

**Symptom:** Keyboard connected but no response when typing.

**Possible causes and fixes:**

1. **Wrong port:** The keyboard must be connected to the USB-C port (via OTG adapter),
   not the USB-A ports. The USB-A ports use VL805 xHCI which is not supported.

2. **No OTG adapter:** You need a USB-C to USB-A OTG adapter between the keyboard
   and the Pi 4.

3. **Unsupported keyboard:** Only boot protocol keyboards are supported. Most standard
   keyboards work, but some gaming keyboards with custom protocols may not.

4. **USB init failed:** Check the serial console for `[INIT] USB HID: init failed`.

---

## Known Limitations

### Hardware Limitations

| Limitation | Description |
|------------|-------------|
| No USB-A keyboard | VL805 xHCI controller not implemented. Use USB-C OTG only. |
| No WiFi/Bluetooth | Wireless drivers not implemented. |
| No HDMI output | No display driver. All output via serial console. |
| No audio | Audio drivers not implemented. |
| No GPIO access | General-purpose GPIO not exposed to userspace. |
| Single SD card | No USB storage support. |

### Software Limitations

| Limitation | Description |
|------------|-------------|
| No filesystem | seL4 runs as a single rootserver. No file operations. |
| No multitasking | Single-process execution model. |
| No persistent storage | seL4 side cannot write configuration that survives reboot. |
| No TCP/UDP | Only ARP and ICMP networking protocols. |
| No DHCP | Static IP only (10.0.0.2/24). |
| No DNS | Must use numeric IP addresses. |
| No SSH/Telnet | Remote access only via serial UART. |
| Static IP only | IP address is compiled into the kernel image. |
| 240-byte payload limit | UART IPC messages limited to 240 bytes payload. |
| Single user | No authentication or user accounts. |

### AI Limitations

| Limitation | Description |
|------------|-------------|
| No on-device AI | All AI inference runs on the host PC. |
| UART bottleneck | Cache misses cost 7ms round-trip (vs 54us shared memory). |
| Llama 3.2 1B only | Host PC without GPU limited to smaller models. |
| No learning | Decision cache is static (pre-compiled patterns). |

---

## Architecture Reference

### Memory Map (Virtual Addresses)

```
0x400000-0x5BFFFF  Application code and data
0x5C0000           UART PL011 MMIO
0x5C1000           GPIO MMIO
0x5C2000           System Timer MMIO
0x5C3000           EMMC/SDHCI MMIO
0x5C4000-0x603FFF  DMA buffer pool (256KB, uncacheable)
0x604000-0x609FFF  GENET Ethernet MMIO (6 pages)
0x60A000-0x60CFFF  DWC2 USB MMIO (3 pages)
```

### Physical Address Map (BCM2711)

```
0xFC000000  GENET Ethernet controller (separate device untyped)
0xFD580000  GENET Ethernet (alternative mapping)
0xFE000000  Main peripheral base
0xFE003000  System Timer (1 MHz free-running counter)
0xFE200000  GPIO
0xFE201000  UART0 (PL011)
0xFE340000  EMMC/SDHCI
0xFE980000  DWC2 USB OTG controller
```

### Boot Flow

```
1. Pi 4 powers on
2. GPU firmware loads (start4.elf + fixup4.dat)
3. GPU reads config.txt, loads u-boot.bin at 0x80000
4. U-Boot runs, 3-second countdown
5. U-Boot reads boot.scr, loads kernel8.img at 0x80000
6. seL4 kernel initializes, drops to user space
7. JARVIS rootserver starts: banner, driver init, test suite
8. IPC main loop: accepts UART IPC + USB keyboard input
```

### Driver Stack

| Driver | Source File | Hardware |
|--------|-----------|----------|
| UART PL011 | `drivers/uart_pl011.c` | BCM2711 PL011 UART0 |
| EMMC/SDHCI | `drivers/emmc_sdhci.c` | BCM2711 EMMC2 (SDHCI) |
| BCM2711 Timer | `drivers/bcm2711_timer.c` | System Timer (1 MHz) |
| GENET Ethernet | `drivers/bcm_genet.c` | BCM2711 GENET v5 + BCM54213PE PHY |
| Net Stack | `drivers/net_stack.c` | ARP + ICMP (software) |
| Net Commands | `drivers/net_cmd.c` | Shell command dispatch (software) |
| DWC2 USB HID | `drivers/usb_hid.c` | Synopsys DWC2 OTG + HID keyboard |
| Slot Allocator | `drivers/slot_alloc.c` | seL4 CNode slot management |
| DMA Allocator | `drivers/dma_alloc.c` | Uncacheable DMA buffer management |
| Block Device | `drivers/blk_dev.c` | Block device abstraction |

---

## Appendix: File Listing

### Repository Structure

```
JARVIS_OS/
+-- phase2/
|   +-- docs/
|   |   +-- PHASE_2_KICKOFF.md         Phase 2 goals and architecture
|   |   +-- UART_IPC_PROTOCOL.md       Full IPC protocol specification
|   |   +-- SD_CARD_SETUP.md           SD card preparation guide
|   |   +-- USER_GUIDE.md              This file
|   |   +-- ALPHA_TESTER_GUIDE.md      Alpha testing instructions
|   +-- firmware/
|   |   +-- kernel8.img                Pre-built JARVIS seL4 kernel
|   |   +-- u-boot.bin                 U-Boot 2026.01 bootloader
|   |   +-- boot.scr                   U-Boot boot script
|   |   +-- start4.elf                 GPU firmware
|   |   +-- fixup4.dat                 Memory configuration
|   |   +-- bcm2711-rpi-4-b.dtb        Device tree blob
|   |   +-- config.txt                 Boot configuration
|   +-- src/
|   |   +-- sel4/main_arm64.c          Main entry point
|   |   +-- drivers/                   All hardware drivers
|   |   +-- ipc/                       IPC ring buffers and handler
|   |   +-- ai/                        Python AI client and tests
|   +-- scripts/
|   |   +-- build_and_copy_kernel.sh   Build script (WSL/Linux)
|   +-- weeks/                         Weekly status documents
+-- CLAUDE.md                          Project documentation
+-- JARVIS_UNIFIED_PLAN.md             36-month master plan
```
