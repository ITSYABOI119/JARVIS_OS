# UART IPC Protocol Specification

**JARVIS AI-OS Phase 2 - Raspberry Pi 4 Communication**

Version: 1.0
Created: Week 31 (Pre-hardware Prep)
Status: Draft (to be validated on hardware)

---

## Overview

This document specifies the IPC protocol for communication between the Python AI agent (running on host/laptop) and the seL4 microkernel (running on Raspberry Pi 4) via UART serial connection.

### Physical Layer

| Parameter | Value |
|-----------|-------|
| Interface | PL011 UART0 (BCM2711) |
| Baud Rate | 115200 bps |
| Data Bits | 8 |
| Parity | None |
| Stop Bits | 1 |
| Flow Control | None (software XON/XOFF optional) |
| Voltage | 3.3V TTL (USB-serial adapter) |

### Hardware Connection

```
Raspberry Pi 4 GPIO Header          USB-Serial Adapter (CP2102/CH340)
┌─────────────────────────────┐    ┌─────────────────────────────┐
│  Pin 8  (GPIO14/TXD) ───────────> RXD                         │
│  Pin 10 (GPIO15/RXD) <─────────── TXD                         │
│  Pin 6  (GND)        ───────────> GND                         │
└─────────────────────────────┘    └─────────────────────────────┘
                                              │
                                              ▼
                                   Host Computer USB Port
                                   (/dev/ttyUSB0 on Linux)
```

**Important:** Do NOT connect 5V/VCC. The Pi 4 is powered separately.

---

## Message Format

### Frame Structure

All messages use a fixed-length header with variable-length payload:

```
┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────────┬──────────┐
│  SYNC    │  TYPE    │  SEQ     │  LENGTH  │  FLAGS   │  PAYLOAD     │  CRC16   │
│  2 bytes │  1 byte  │  2 bytes │  2 bytes │  1 byte  │  0-240 bytes │  2 bytes │
└──────────┴──────────┴──────────┴──────────┴──────────┴──────────────┴──────────┘
```

| Field | Size | Description |
|-------|------|-------------|
| SYNC | 2 bytes | Magic bytes: `0xAA 0x55` |
| TYPE | 1 byte | Message type (see below) |
| SEQ | 2 bytes | Sequence number (little-endian, wraps at 65535) |
| LENGTH | 2 bytes | Payload length (0-240, little-endian) |
| FLAGS | 1 byte | Bit flags (see below) |
| PAYLOAD | 0-240 bytes | Variable-length data |
| CRC16 | 2 bytes | CRC-16-CCITT over TYPE through PAYLOAD |

**Header Size (after SYNC):** 6 bytes (TYPE + SEQ + LENGTH + FLAGS)
**Maximum Frame Size:** 248 bytes (2 SYNC + 6 header + 240 payload + 2 CRC)

### Message Types

| Value | Name | Direction | Description |
|-------|------|-----------|-------------|
| 0x01 | QUERY | Host→Pi | Cache lookup query |
| 0x02 | RESPONSE | Pi→Host | Cache lookup response |
| 0x03 | HEARTBEAT | Bidirectional | Keep-alive ping |
| 0x04 | HEARTBEAT_ACK | Bidirectional | Keep-alive response |
| 0x05 | STATS_REQUEST | Host→Pi | Request cache statistics |
| 0x06 | STATS_RESPONSE | Pi→Host | Cache statistics data |
| 0x07 | COMMAND | Host→Pi | Shell command execution |
| 0x08 | COMMAND_RESULT | Pi→Host | Command execution result |
| 0x10 | SHIELD_CHECK | Host→Pi | SHIELD risk assessment request |
| 0x11 | SHIELD_RESULT | Pi→Host | SHIELD risk score response |
| 0x20 | STATE_CHANGE | Pi→Host | System state notification |
| 0x21 | STATE_ACK | Host→Pi | State change acknowledgment |
| 0xFE | ERROR | Bidirectional | Error response |
| 0xFF | RESET | Bidirectional | Protocol reset request |

### Flag Bits

| Bit | Name | Description |
|-----|------|-------------|
| 0 | ACK_REQ | Acknowledgment required |
| 1 | PRIORITY | High-priority message |
| 2 | FRAGMENT | Fragmented message (more fragments follow) |
| 3 | LAST_FRAG | Last fragment of fragmented message |
| 4-7 | Reserved | Must be 0 |

---

## Message Payloads

### QUERY (0x01)

Request a cache lookup for the given query string.

```
┌──────────────────────────────────────────────────────┐
│  Query String (null-terminated, max 239 bytes)       │
└──────────────────────────────────────────────────────┘
```

Example: `"list files\0"`

### RESPONSE (0x02)

Response to a QUERY message.

```
┌──────────┬──────────┬────────────────────────────────┐
│  STATUS  │  HIT     │  Action String (null-term)     │
│  1 byte  │  1 byte  │  max 237 bytes                 │
└──────────┴──────────┴────────────────────────────────┘
```

| STATUS Value | Meaning |
|--------------|---------|
| 0x00 | Success |
| 0x01 | Cache miss |
| 0x02 | Invalid query |
| 0xFF | Error |

| HIT Value | Meaning |
|-----------|---------|
| 0x00 | Cache miss (AI required) |
| 0x01 | Cache hit |

Example Response:
```
STATUS=0x00, HIT=0x01, Action="ACTION:exec_ls|TRUST:0"
```

### HEARTBEAT (0x03)

Keep-alive message sent every 5 seconds.

```
┌──────────────────────────────────────────────────────┐
│  Timestamp (8 bytes, milliseconds since boot)        │
└──────────────────────────────────────────────────────┘
```

### HEARTBEAT_ACK (0x04)

Response to HEARTBEAT.

```
┌──────────────────────────────────────────────────────┐
│  Original Timestamp (8 bytes, echo back)             │
│  Response Timestamp (8 bytes, receiver's time)       │
└──────────────────────────────────────────────────────┘
```

### STATS_RESPONSE (0x06)

Cache statistics response.

```
┌──────────┬──────────┬──────────┬──────────┬──────────┐
│  Hits    │  Misses  │  Entries │  Patterns│  Uptime  │
│  4 bytes │  4 bytes │  4 bytes │  4 bytes │  4 bytes │
└──────────┴──────────┴──────────┴──────────┴──────────┘
```

All values are little-endian 32-bit unsigned integers.

### ERROR (0xFE)

Error response.

```
┌──────────┬────────────────────────────────────────────┐
│  CODE    │  Error Message (null-terminated)           │
│  1 byte  │  max 238 bytes                             │
└──────────┴────────────────────────────────────────────┘
```

| Error Code | Meaning |
|------------|---------|
| 0x01 | Invalid message type |
| 0x02 | CRC mismatch |
| 0x03 | Payload too large |
| 0x04 | Timeout |
| 0x05 | Sequence error |
| 0x10 | Cache error |
| 0x11 | Command execution error |
| 0xFF | Unknown error |

---

## CRC-16 Algorithm

Use CRC-16-CCITT (polynomial 0x1021, initial value 0xFFFF).

```c
uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}
```

The CRC is calculated over bytes from TYPE through end of PAYLOAD (excludes SYNC bytes).

---

## Protocol State Machine

```
                    ┌─────────────────┐
                    │                 │
                    ▼                 │
              ┌──────────┐            │
     ┌───────>│   IDLE   │────────────┘
     │        └──────────┘     Timeout (no heartbeat for 30s)
     │              │
     │              │ Receive SYNC bytes
     │              ▼
     │        ┌──────────┐
     │        │ HEADER   │──────────┐
     │        └──────────┘          │ Invalid header
     │              │               │
     │              │ Valid header  ▼
     │              ▼          ┌─────────┐
     │        ┌──────────┐     │  ERROR  │
     │        │ PAYLOAD  │     └─────────┘
     │        └──────────┘          │
     │              │               │
     │              │ All bytes     │
     │              ▼               │
     │        ┌──────────┐          │
     │        │   CRC    │──────────┘
     │        └──────────┘     CRC mismatch
     │              │
     │              │ CRC valid
     │              ▼
     │        ┌──────────┐
     └────────│ PROCESS  │
              └──────────┘
```

---

## Timing Requirements

| Parameter | Value | Notes |
|-----------|-------|-------|
| Heartbeat Interval | 5 seconds | Send HEARTBEAT if no traffic |
| Heartbeat Timeout | 30 seconds | Consider link dead if no heartbeat |
| Response Timeout | 500 ms | Time to wait for RESPONSE after QUERY |
| Retry Count | 3 | Retransmit on timeout |
| Inter-byte Timeout | 100 ms | Max gap between bytes in frame |

---

## Byte Rate Calculations

At 115200 baud with 8N1 (10 bits per byte):

| Metric | Value |
|--------|-------|
| Bytes per second | 11,520 |
| Max frame (250 bytes) | 21.7 ms |
| Typical query (50 bytes) | 4.3 ms |
| Round-trip (query + response) | ~10-20 ms |

**Expected IPC Latency:** 5-15 ms typical (vs 54μs for shared memory)

This is 100-300x slower than ivshmem shared memory, but acceptable for
non-real-time AI queries. The decision cache handles the performance-critical
path with <1ms lookups.

---

## Error Recovery

### CRC Error

1. Receiver sends ERROR with code 0x02
2. Receiver discards frame
3. Sender retransmits after 100ms

### Timeout

1. If no response within 500ms, sender retransmits
2. After 3 retries, report error to upper layer
3. If persistent, send RESET and reinitialize

### Sequence Error

1. If sequence number jumps, log warning
2. Process message anyway (best effort)
3. Reset sequence tracking on RESET message

### Link Loss

1. If no heartbeat for 30 seconds, consider link dead
2. Close and reopen serial port
3. Send RESET to resynchronize

---

## Example Transaction

### Cache Lookup Query

```
Host → Pi:
  SYNC:   AA 55
  TYPE:   01 (QUERY)
  SEQ:    01 00 (sequence 1)
  LENGTH: 0B 00 (11 bytes)
  FLAGS:  01 (ACK_REQ)
  PAYLOAD: "list files\0"
  CRC16:  XX XX

Pi → Host:
  SYNC:   AA 55
  TYPE:   02 (RESPONSE)
  SEQ:    01 00 (matching sequence)
  LENGTH: 1C 00 (28 bytes)
  FLAGS:  00
  PAYLOAD: 00 01 "ACTION:exec_ls|TRUST:0\0"
  CRC16:  XX XX
```

---

## Implementation Notes

### seL4 Side (C)

- Use ring buffer for TX/RX queuing
- Poll-based (no interrupts initially, add later)
- Integrate with existing IPC handler

### Python Side

- Use pyserial for serial port access
- Threading: separate RX thread with queue
- Timeout handling with select()

### Testing

1. Loopback test (short TX to RX)
2. Echo test (Pi echoes all received bytes)
3. Protocol test (full message exchange)
4. Stress test (rapid message bursts)

---

## Future Extensions

1. **Interrupt Mode:** Add IRQ-driven RX for lower latency
2. **DMA Transfer:** Use DMA for bulk data transfers
3. **Compression:** LZ4 compression for large payloads
4. **Encryption:** Optional AES-128 for sensitive data
5. **Higher Baud:** Test 230400 or 460800 baud

---

## References

- BCM2711 ARM Peripherals Datasheet (Chapter 11: UART)
- ARM PrimeCell UART (PL011) Technical Reference Manual
- CRC-16-CCITT Specification (ITU-T V.41)
- Phase 1 IPC Protocol (ring_buffer.h, ipc_client.py)
