# JARVIS AI-OS Alpha Tester Guide

**Version:** 0.2 Alpha - Phase 2
**Platform:** Raspberry Pi 4 (BCM2711) + Host PC
**Last Updated:** February 2026

---

## What Alpha Means

JARVIS AI-OS is currently in **Alpha** status. This means:

- Core drivers are hardware-verified and pass all tests
- The system boots reliably on Raspberry Pi 4 Model B (8GB)
- Basic functionality works: UART IPC, decision cache, networking, USB keyboard
- **Expect rough edges:** limited features, no filesystem, no TCP/UDP, no WiFi
- **Not for production use:** This is a development milestone for testing and feedback

The purpose of alpha testing is to validate driver reliability, IPC stability, and
boot consistency across different hardware configurations and environments.

---

## Getting Started

### Prerequisites

Before testing, ensure you have:

1. Raspberry Pi 4 Model B (4GB or 8GB)
2. microSD card (16GB+, Class 10) with boot files installed
3. USB-to-serial adapter (CP2102, CH340, or FTDI, 3.3V TTL)
4. 3 female-to-female jumper wires
5. USB-C power supply (5V 3A)
6. Host PC with Python 3.8+ and pyserial installed
7. Serial terminal software (PuTTY, minicom, screen, picocom)

For complete setup instructions, see the **User Guide** (`phase2/docs/USER_GUIDE.md`).

### Quick Start

1. Flash the SD card with files from `phase2/firmware/`
2. Wire UART: Pi GPIO14 (Pin 8) -> adapter RX, Pi GPIO15 (Pin 10) -> adapter TX, Pi GND (Pin 6) -> adapter GND
3. Open serial terminal at 115200 baud, 8N1
4. Power on the Pi 4
5. Wait for "UART IPC Handler Running" message
6. Connect with Python client or type commands via USB keyboard

---

## What to Test

### Priority 1: Boot Reliability

Test that the system boots cleanly and all tests pass.

**Steps:**
1. Power on the Pi 4 with serial terminal connected
2. Observe the full boot sequence
3. Record the test results summary

**Expected:** 46 PASS, 0 FAIL, 3 SKIP

**Report if:**
- Any tests show FAIL
- Boot hangs at any point (note the last message displayed)
- Boot takes longer than 10 seconds from power-on to IPC handler ready
- Garbled or unexpected output appears

**Repeat:** Power cycle 10 times and record results for each boot.

### Priority 2: UART IPC Stability

Test that the Python AI client communicates reliably with the Pi 4.

**Steps:**
1. Boot the Pi 4 and wait for IPC handler ready
2. Connect the Python client:
   ```python
   from phase2.src.ai.uart_ipc_client import UARTIPCClient
   client = UARTIPCClient('/dev/ttyUSB0')
   client.connect()
   ```
3. Send 100 cache lookup queries:
   ```python
   for i in range(100):
       result = client.cache_lookup("list files")
       assert result['hit'] == True
   ```
4. Send heartbeats and verify responses
5. Leave connected for 1 hour and check for disconnections

**Expected:** 100% success rate, no timeouts, no CRC errors.

**Report if:**
- Any query returns an error or times out
- CRC mismatch errors appear
- Connection drops during sustained operation
- Response latency exceeds 50ms for cache hits

### Priority 3: Network Commands

Test networking with an Ethernet cable connected.

**Steps:**
1. Connect Ethernet cable between Pi 4 and a switch/router on the 10.0.0.0/24 subnet
2. Configure a host on the same subnet (e.g., set your PC to 10.0.0.1/24)
3. Send commands via Python client:
   ```python
   # Check interface
   result = client.send_command("ifconfig")
   print(result)  # Should show UP, MAC, IP, speed

   # Check stats
   result = client.send_command("netstat")
   print(result)

   # Ping the gateway
   result = client.send_command("ping 10.0.0.1")
   print(result)  # Should show replies with RTT
   ```

**Expected:**
- `ifconfig` shows eth0 UP with correct IP (10.0.0.2) and speed
- `netstat` shows TX/RX packet counts
- `ping` receives replies with sub-millisecond RTT on gigabit

**Report if:**
- Link shows DOWN despite cable being connected
- Ping shows 100% packet loss to a known-reachable host
- MAC address shows all zeros
- ARP timeout when the target is on the same subnet

### Priority 4: USB Keyboard

Test USB keyboard input via USB-C OTG adapter (optional, requires adapter).

**Steps:**
1. Connect USB keyboard via USB-C OTG adapter to the Pi 4 USB-C port
2. Boot the Pi 4
3. After "UART IPC Handler Running" appears, type `ifconfig` and press Enter
4. Verify the output appears on the serial console
5. Test special keys: Backspace, Ctrl+C, Caps Lock

**Expected:**
- Characters echo on serial console as you type
- Enter dispatches the command
- Backspace deletes the previous character
- Ctrl+C cancels the current line

**Report if:**
- No echo when typing (keyboard not detected)
- Keys produce wrong characters
- Keyboard works intermittently
- `[INIT] USB HID: init failed` appears during boot

### Priority 5: Cache Hit Rate

Verify the decision cache performance.

**Steps:**
1. Connect with Python client
2. Send STATS_REQUEST:
   ```python
   stats = client.get_stats()
   print(f"Hits: {stats['hits']}, Misses: {stats['misses']}")
   print(f"Hit rate: {stats['hits']/(stats['hits']+stats['misses'])*100:.1f}%")
   ```
3. Send a variety of queries and observe hit/miss ratio

**Expected:** ~85% hit rate for standard system queries.

**Report if:**
- Hit rate is below 70%
- Cache returns incorrect actions for known queries
- Cache lookup takes more than 5ms (should be <1ms)

---

## Test Matrix

Test these combinations if possible:

| Pi 4 Model | RAM | SD Card | USB-Serial | Ethernet | USB Keyboard | Priority |
|------------|-----|---------|------------|----------|-------------|----------|
| Model B | 8GB | 32GB Class 10 | CP2102 | Yes | No | HIGH |
| Model B | 4GB | 16GB Class 10 | CH340 | Yes | No | HIGH |
| Model B | 8GB | 32GB Class 10 | FTDI | No | No | MEDIUM |
| Model B | 8GB | 64GB U1 | CP2102 | Yes | Yes (OTG) | MEDIUM |
| Model B | 2GB | 16GB Class 10 | CP2102 | No | No | LOW |
| Model B | 8GB | 128GB U3 | Any | Yes | Yes (OTG) | LOW |

Different SD card brands/sizes help identify compatibility issues.
Different USB-serial adapters help identify UART timing issues.

---

## Bug Reporting

### Where to Report

File issues on the JARVIS AI-OS GitHub repository.

### Required Information

Every bug report must include:

1. **Hardware Configuration**
   - Pi 4 model and RAM size
   - SD card brand, size, and speed class
   - USB-serial adapter model (CP2102, CH340, FTDI, other)
   - Ethernet connected? (yes/no, if yes: direct or via switch)
   - USB keyboard connected? (yes/no, if yes: model)
   - Pi 4 hardware revision (printed on the PCB or from `cat /proc/cpuinfo` on Raspberry Pi OS)

2. **Serial Log**
   - Full serial console output from power-on to the point of failure
   - Use `screen -L /dev/ttyUSB0 115200` (Linux) to capture to a log file
   - Include the complete test suite output

3. **SD Card Contents**
   - List all files on the SD card boot partition with sizes
   - `ls -la` output from the SD card mount point
   - Verify kernel8.img size matches the one in `phase2/firmware/`

4. **Steps to Reproduce**
   - Exact sequence of actions that trigger the bug
   - Whether the bug is reproducible (always, sometimes, once)

5. **Expected vs Actual Behavior**
   - What you expected to happen
   - What actually happened

### Bug Report Template

```
## Bug Report

### Environment
- Pi 4: Model B, 8GB
- SD Card: SanDisk Ultra 32GB Class 10
- USB-Serial: CP2102
- Ethernet: Connected via switch
- USB Keyboard: None
- Pi 4 Revision: c03115

### Description
[Brief description of the issue]

### Steps to Reproduce
1. [Step 1]
2. [Step 2]
3. [Step 3]

### Expected Behavior
[What should happen]

### Actual Behavior
[What actually happens]

### Serial Log
```
[Paste full serial output here]
```

### SD Card Contents
```
[Paste ls -la output here]
```
```

---

## Known Issues

### Active Issues

| Issue | Severity | Description | Workaround |
|-------|----------|-------------|------------|
| USB-A ports not supported | Low | VL805 xHCI driver not implemented | Use USB-C OTG adapter |
| Static IP only | Low | No DHCP client | Configure network for 10.0.0.0/24 |
| No filesystem | Medium | Cannot read/write files from seL4 | SD card access only at boot (EMMC driver reads raw blocks) |
| 240-byte command output | Low | Shell command output truncated at 240 bytes | Limit query scope |
| No fragmentation | Low | IP fragmentation not implemented | Keep ping payload under MTU |

### Expected SKIPs

These test SKIPs are normal and not bugs:

| Test | Condition for SKIP |
|------|-------------------|
| `cmd_ping_gateway` | No Ethernet cable connected |
| `usb_enumeration` | No USB device connected |
| `hid_poll` | No USB keyboard connected |

### Resolved Issues (for reference)

| Issue | Resolved In | Description |
|-------|-------------|-------------|
| RX index wrap at 65536 | Week 38 bugfix | RX consumer index now masked with DMA_INDEX_MASK |
| RBUF prepend offset | Week 38 bugfix | 66-byte RBUF prepend correctly stripped from RX frames |
| ICMP bounds check | Week 38 bugfix | memcpy clamped to actual received length |

---

## Performance Baselines

Record these metrics during testing for comparison:

| Metric | Expected | Your Result |
|--------|----------|-------------|
| Boot time (power to IPC ready) | ~2 seconds | _______ |
| Test suite result | 46 PASS, 0 FAIL, 3 SKIP | _______ |
| UART IPC RTT (cache hit) | <10ms | _______ |
| Decision cache hit rate | ~85.7% | _______ |
| EMMC throughput (ADMA2) | ~11.9 MB/s | _______ |
| Ethernet link speed | 1000 Mbps Full | _______ |
| Ping RTT (same subnet) | <1ms | _______ |
| 100-query stress test | 100% success | _______ |

---

## Feedback Channels

- **GitHub Issues:** Primary channel for bug reports and feature requests
- **Serial Logs:** Always include serial output with reports
- **Performance Data:** Share your performance baseline numbers

---

## Testing Schedule

Alpha testing is ongoing during Phase 2 (Weeks 40-52). Key milestones:

| Weeks | Focus |
|-------|-------|
| 40-41 | USB HID keyboard driver (COMPLETE) |
| 42 | Alpha release packaging, documentation |
| 43-46 | Alpha release infrastructure, additional testing |
| 47-50 | Security audit preparation |
| 50-52 | 30-day stability testing |

### What Testers Should Focus On

- **Now:** Boot reliability, UART IPC stability, basic networking
- **Week 43+:** Extended stability testing (24+ hour runs)
- **Week 47+:** Security-focused testing (malformed packets, protocol fuzzing)

---

## FAQ

**Q: Can I use Raspberry Pi OS alongside JARVIS?**
A: No. JARVIS replaces the entire OS with seL4 bare metal. The SD card contains
only boot firmware and the JARVIS kernel image. There is no Linux involved.

**Q: Can I use a Pi 3, Pi 5, or Pi Zero?**
A: No. JARVIS is built specifically for the BCM2711 (Pi 4). Other Pi models have
different SoCs with different peripheral layouts and are not supported.

**Q: Why is there no display output?**
A: JARVIS runs headless. All output goes through the UART serial console. HDMI
drivers are not implemented because the primary interface is the AI agent via UART.

**Q: Can I change the IP address?**
A: Not without rebuilding the kernel. The IP address (10.0.0.2) is compiled into
the networking stack. This will be configurable in a future release.

**Q: What happens if the serial cable is disconnected while running?**
A: The Pi 4 continues running. The Python client will detect a timeout after
30 seconds and report a link-dead condition. Reconnecting and restarting the
Python client will re-establish communication. No reboot needed.

**Q: Is the SD card at risk of corruption?**
A: The EMMC driver performs read and write operations, but normal operation only
reads the SD card (for the decision cache patterns at boot). Write tests are
disabled by default (EMMC_WRITE_TEST_ENABLE=0). Power cycling during a write
could corrupt the SD card, but this is unlikely during normal operation.

**Q: How do I update to a new version?**
A: Copy the new `kernel8.img` to the SD card boot partition, replacing the old one.
No other files need to change unless the release notes say otherwise.
