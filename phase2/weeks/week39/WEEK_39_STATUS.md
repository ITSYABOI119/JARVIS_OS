# Week 39 Status: Shell Commands + GENET Integration Tests

**Status:** HARDWARE VERIFIED (February 7, 2026)
**Goal:** Network shell commands (ping/ifconfig/netstat) + GENET integration tests

---

## Summary

Added outbound networking capabilities (ARP request, ICMP echo request), ARP
cache, and shell-style commands (ping, ifconfig, netstat) callable from UART IPC.
Includes COMMAND message handler in IPC loop and 10 new hardware tests.

## Achievements

1. **GENET link status/speed/stats** (bcm_genet.h/c)
   - `genet_get_link_status()` - PHY BMSR double-read for latching-low
   - `genet_get_link_speed()` - ANLPAR/STAT1000 negotiated speed
   - `genet_stats_t` counters in TX/RX paths
   - MII ANLPAR + 1000BASE-T status register defines

2. **Outbound networking** (net_stack.h/c)
   - ARP cache (8 entries, FIFO eviction)
   - `net_build_arp_request()` - 42-byte ARP WHO-HAS frame builder
   - `net_build_icmp_request()` - 74-byte ICMP echo request builder
   - `net_arp_resolve()` - send ARP + poll with timeout
   - `net_is_icmp_echo_reply()` - match reply by ident/seq
   - `net_process_arp_reply()` - learn from ARP replies
   - `net_get_ip()`/`net_get_mac()` getters

3. **Shell commands** (net_cmd.h/c - NEW)
   - `cmd_ping(ip, count, timeout)` - ICMP ping with RTT measurement
   - `cmd_ifconfig()` - MAC, IP, link status/speed display
   - `cmd_netstat()` - TX/RX statistics + ARP cache count
   - `cmd_dispatch(str)` - parse command string, route to handler
   - `parse_ipv4("a.b.c.d")` - dotted-decimal to network-order uint32

4. **UART IPC COMMAND handler** (main_arm64.c)
   - MSG_TYPE_COMMAND (0x07) / MSG_TYPE_COMMAND_RESULT (0x08)
   - `handle_command()` - dispatch and return result via UART

5. **Test suite** - 10 new tests (32 PASS + 1 SKIP: 12 EMMC + 6 TX + 5 RX/Net + 9 Cmd PASS + 1 SKIP)

## New Files

| File | LOC | Purpose |
|------|-----|---------|
| net_cmd.h | ~40 | Shell command API |
| net_cmd.c | ~270 | ping, ifconfig, netstat implementation |

## Modified Files

| File | Change |
|------|--------|
| bcm_genet.h | Link speed enum, stats struct, 4 new API prototypes, MII register defines |
| bcm_genet.c | Stats counters in TX/RX, link status/speed/stats functions |
| net_stack.h | ARP cache struct, outbound builder prototypes, resolve/match API |
| net_stack.c | ARP cache, frame builders, resolve, reply matching (~200 LOC added) |
| main_arm64.c | COMMAND handler, 10 Week 39 tests (~250 LOC added) |
| CMakeLists.txt | Added net_cmd.c |

## Test Plan

| # | Test | Type | Cable? | Expected |
|---|------|------|--------|----------|
| 1 | link_status | Unit | No | PASS (UP or DOWN) |
| 2 | link_speed | Unit | No | PASS (valid enum) |
| 3 | stats_counter | Unit | No | PASS (tx_packets > 0) |
| 4 | arp_cache_hit | Unit | No | PASS |
| 5 | arp_cache_miss | Unit | No | PASS |
| 6 | arp_request_build | Unit | No | PASS (42 bytes, correct structure) |
| 7 | icmp_request_build | Unit | No | PASS (74 bytes, valid checksum) |
| 8 | cmd_ifconfig | Integration | No | PASS (output contains eth0/MAC/IP/Link) |
| 9 | cmd_netstat | Integration | No | PASS (output contains TX/RX/ARP) |
| 10 | cmd_ping_gateway | Hardware | Yes | PASS or SKIP (no link) |

## Hardware Test Results

```
Week 39 Tests: 9 PASS, 0 FAIL, 1 SKIP
  link_status=PASS  link_speed=PASS  stats_counter=PASS
  arp_cache_hit=PASS  arp_cache_miss=PASS  arp_request_build=PASS
  icmp_request_build=PASS  cmd_ifconfig=PASS  cmd_netstat=PASS
  cmd_ping_gateway=SKIP (link DOWN - expected without cable)

Total: 32 PASS, 0 FAIL, 1 SKIP (12 EMMC + 6 TX + 5 RX/Net + 9 Cmd + 1 SKIP)
```

### Bug Fixed During Testing
- Test 7 `icmp_request_build` initially FAILED: IP checksum verification in test had
  byte-order bug (unnecessary byte swap of host-endian value). Fixed by removing swap.

## Next Steps

1. End-to-end ping test with Ethernet cable (test 10 → PASS)
2. Python IPC test: `send_command("ping 192.168.1.1")`
3. Week 40: Additional Tier 1 drivers

---

*Created: February 7, 2026*
*Hardware verified: February 7, 2026*
