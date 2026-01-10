# Week 34 Status: Python↔seL4 IPC Testing

**Status:** PENDING
**Target Start:** January 11, 2026
**Estimated Effort:** 8-12 hours

---

## Overview

Week 34 validates end-to-end Python↔seL4 communication via UART. With UART RX now enabled (Week 33), this week tests the complete IPC round-trip from Python on the host PC to the seL4 decision cache on the Pi 4.

---

## Prerequisites (from Week 33) ✅

- UART TX working via seL4_DebugPutChar()
- UART RX enabled via device frame mapping at 0x5c0000
- Decision cache loaded (258 patterns)
- IPC handler running on Pi 4

---

## Objectives

### 1. Test UART RX with Manual Input
- [ ] Connect PuTTY to USB-UART adapter (115200, 8N1)
- [ ] Type characters, verify echo on seL4 side
- [ ] Confirm byte-by-byte reception working

### 2. Connect Python UART Client
- [ ] Configure `uart_ipc_client.py` for `/dev/ttyUSB0` (or COM port on Windows)
- [ ] Establish connection with handshake
- [ ] Verify heartbeat exchange (every 5 seconds)

### 3. Send UART IPC Query Frame
- [ ] Construct frame: 0xAA55 sync + QUERY type + payload
- [ ] Send query: "list files"
- [ ] Capture raw bytes on seL4 side for debugging

### 4. Verify Cache Lookup Response
- [ ] seL4 receives frame, parses query
- [ ] Decision cache lookup executes
- [ ] Response frame sent back with cache hit/miss
- [ ] Python receives and decodes response

### 5. Measure Round-Trip Latency
- [ ] Timestamp query send and response receive
- [ ] Calculate round-trip time
- [ ] Target: 10-20ms
- [ ] Acceptable: <25ms

### 6. Validate Cache Hit Rate
- [ ] Send 50+ queries from test set
- [ ] Count cache hits vs misses
- [ ] Target: >80% hit rate
- [ ] Compare with QEMU baseline (85.7%)

---

## Success Criteria

| Criterion | Target | Status |
|-----------|--------|--------|
| Python→seL4 query received | Frame parsed correctly | ⏳ |
| seL4→Python response received | Valid response frame | ⏳ |
| Cache hit rate | >80% | ⏳ |
| Round-trip latency | <25ms | ⏳ |
| Zero frame errors | CRC validation passes | ⏳ |
| Heartbeat stable | 5-second interval maintained | ⏳ |

---

## Test Plan

### Phase 1: Manual Testing (2-3 hours)
1. PuTTY character echo test
2. Frame construction verification
3. Single query/response cycle

### Phase 2: Automated Testing (3-4 hours)
1. Run `test_uart_ipc_client.py` against real hardware
2. Stress test: 100 queries in rapid succession
3. Error injection: malformed frames, CRC errors

### Phase 3: Performance Measurement (2-3 hours)
1. Latency profiling (100 samples)
2. Cache hit rate analysis
3. Throughput measurement (queries/second)

---

## Hardware Setup

```
PC (Windows/WSL)              Pi 4 (seL4)
────────────────              ───────────
Python UART Client   USB      UART RX (GPIO15)
uart_ipc_client.py ◄─────────► PL011 at 0xFE201000
                    UART
                    115200 baud
```

**USB-UART Adapter Wiring:**
- Adapter TX → Pi GPIO15 (RXD)
- Adapter RX → Pi GPIO14 (TXD)
- Adapter GND → Pi GND

---

## Files to Modify/Create

| File | Action | Purpose |
|------|--------|---------|
| `uart_ipc_client.py` | Modify | Add real hardware mode |
| `test_hardware_ipc.py` | Create | Hardware-specific tests |
| `WEEK_34_RESULTS.md` | Create | Document findings |

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| UART RX not receiving | Check GPIO15 wiring, verify device frame mapping |
| Frame sync errors | Add 0xAA55 search with timeout |
| High latency (>25ms) | Profile each stage, optimize bottlenecks |
| Low cache hit rate | Verify pattern loading, check normalization |

---

## Notes

- Week 33 confirmed UART RX is enabled but not yet tested with actual byte reception
- First real Python↔seL4 communication in JARVIS project history
- This validates the split architecture: PC (AI) ↔ Pi 4 (cache)

---

*Week 34 template created January 10, 2026*
*Previous: Week 33 (UART RX Enable) ✅*
*Next: Week 35 (SD/EMMC Driver) ⏳*
