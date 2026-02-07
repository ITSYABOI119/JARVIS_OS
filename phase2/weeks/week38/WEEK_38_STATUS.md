# Week 38 Status: GENET RX + Basic Networking

**Status:** HARDWARE VERIFIED (February 7, 2026) - 5 PASS, 0 FAIL (23 total with EMMC+GENET TX)
**Goal:** RX receive path + ARP/ICMP networking stack

---

## Summary

Added RX DMA ring receive path to GENET driver and created a minimal
networking stack supporting ARP replies and ICMP echo (ping) replies.

## Achievements

1. **GENET RX DMA ring 16** - 32 descriptors, polled receive
   - Pre-allocated 64KB DMA buffer pool (before register writes)
   - genet_rx_ring_init(), genet_rx_recv(), genet_rx_poll()
   - RDMA enable + UMAC RX enable

2. **Basic networking stack** (net_stack.c/h)
   - Ethernet frame parsing and dispatch
   - ARP reply handler (responds to ARP requests for our IP)
   - IPv4 header parsing with checksum validation
   - ICMP echo reply handler (ping response)
   - Ones-complement checksum calculation

3. **Test suite** - RX + networking tests in main_arm64.c
   - rx_ring_init, rx_poll_empty, net_config, arp_reply, icmp_echo_reply

## New Files

| File | Purpose |
|------|---------|
| net_stack.h | Network protocol structs and API |
| net_stack.c | ARP, IP, ICMP handlers |

## Modified Files

| File | Change |
|------|--------|
| bcm_genet.h | RX ring struct, buffer defines, RX API prototypes |
| bcm_genet.c | RX DMA pre-alloc, rx_ring_init, rx_recv, rx_poll |
| CMakeLists.txt | Added net_stack.c |
| main_arm64.c | Week 38 test section |

## Hardware Test Results

All 5 Week 38 tests PASS on Pi 4 bare metal (23 total: 12 EMMC + 6 GENET TX + 5 RX/Net):

| Test | Result |
|------|--------|
| rx_ring_init | PASS - RDMA enabled, 32 descs |
| rx_poll_empty | PASS - No frames (no cable, expected) |
| net_config | PASS - Stack initialized with IP 10.0.0.2 |
| arp_reply | PASS - Correct opcode=2, target MAC verified |
| icmp_echo_reply | PASS - Type=0, ident+seq preserved |

RX DMA: 64KB (16 pages) at paddr 0x014a2000, allocated before GENET MMIO writes.
PHY: BCM54213PE detected (OUI=0x180361), link DOWN (no cable).

## Next Steps (Week 39+)

1. Ping test from host PC to Pi 4 (requires Ethernet cable)
2. USB HID driver planning

---

*Created: February 7, 2026*
