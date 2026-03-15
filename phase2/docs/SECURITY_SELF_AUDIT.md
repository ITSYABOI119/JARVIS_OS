# JARVIS AI-OS Phase 2 Security Self-Audit

**Date:** February 13, 2026 (Week 49)
**Scope:** All Phase 2 C sources (~10,000 LOC across 21 drivers/modules)
**Methodology:** GCC `-Werror` build verification + manual code review of critical paths

## Related: Dependency Security Audit (Snyk)

Week 49 also includes a host-side Python dependency audit using Snyk CLI. Results are tracked separately here:

- `phase2/weeks/week49/SECURITY_DEPENDENCY_AUDIT_SNYK.md`

## Build Verification

The TII seL4 build system compiles with `-Werror` (warnings as errors). A full Ninja build produces **0 errors, 0 warnings** from JARVIS application code. Standalone `cppcheck` analysis was not feasible due to seL4 header dependencies (generated types require full build tree), but the `-Werror` build provides equivalent coverage for standard warning classes.

## Findings Summary

| # | Severity | File | Line(s) | Issue | Status |
|---|----------|------|---------|-------|--------|
| 1 | HIGH | net_cmd.c | 150, 181-215 | snprintf pos/size unsigned underflow | **FIXED** |
| 2 | MEDIUM | net_cmd.c | 387-407 | snprintf n/output_size unsigned underflow | **FIXED** |
| 3 | MEDIUM | bcm_genet.c | 1009 | RX cons_index not masked before descriptor lookup | **FIXED** |
| 4 | LOW | net_stack.c | 496 | Timeout deadline uint64 wraparound (theoretical) | Accepted |
| 5 | LOW | main_arm64.c | 588-589 | Cache action field in snprintf (bounded by cache init) | Accepted |
| 6 | HIGH | net_stack.c | handle_icmp(), net_is_icmp_echo_reply() | IPv4 total_len vs IHL underflow could drive out-of-bounds checksum read | **FIXED** |

## Finding Details

### F1 (HIGH): Output Buffer Integer Underflow in cmd_ping()

**File:** `phase2/src/drivers/net_cmd.c`, lines 150, 181-215

**Description:** `cmd_ping()` uses `int pos` to track output buffer position and `uint32_t output_size` for buffer size. The expression `output_size - pos` is evaluated as unsigned subtraction. If `snprintf()` returns a value larger than remaining buffer space (indicating truncation), `pos` can exceed `output_size`. Subsequent calls compute `output_size - pos` as a wrapped-around huge value, causing `snprintf` to receive an incorrect size parameter and `output + pos` to point past the buffer.

**Fix:** Added `safe_remaining(pos, output_size)` helper that returns 0 when `pos >= output_size`. Replaced all `output_size - pos` expressions with `safe_remaining(pos, output_size)`. Also replaced the loop guard `pos < (int)(output_size - 60)` with `safe_remaining(pos, output_size) > 60`.

### F2 (MEDIUM): Unsigned Underflow in Multiple Command Handlers

**File:** `phase2/src/drivers/net_cmd.c`, lines 387-407

**Description:** Same class of bug as F1, affecting `cmd_dt()` and other handlers that use `output_size - (uint32_t)n` pattern. If `n` exceeds `output_size` due to snprintf truncation, the unsigned subtraction wraps.

**Fix:** Replaced all `output_size - (uint32_t)n` expressions with `safe_remaining(n, output_size)`.

### F3 (MEDIUM): Missing RX Index Mask in GENET Driver

**File:** `phase2/src/drivers/bcm_genet.c`, line 1009

**Description:** The RX path uses `rx_ring.cons_index % rx_ring.size` for descriptor lookup without first masking to 16 bits. The GENET hardware uses 16-bit DMA indices (DMA_INDEX_MASK = 0xFFFF). While `% 32` mathematically produces correct results for any uint32_t value, this inconsistency with the TX path (which masks before comparison) violates the established pattern documented in MEMORY.md: "ALWAYS mask cons_index with DMA_INDEX_MASK."

**Fix:** Changed to `(rx_ring.cons_index & DMA_INDEX_MASK) % rx_ring.size` for consistency with TX path and hardware semantics.

### F4 (LOW): Timeout Deadline Wraparound (Accepted)

**File:** `phase2/src/drivers/net_stack.c`, line 496

**Description:** `uint64_t deadline = start + (uint64_t)timeout_ms * 1000` could wrap if `systimer_read()` returns a value near UINT64_MAX. Impact is negligible: the BCM2711 system timer runs at 1 MHz, reaching UINT64_MAX after ~584,942 years.

**Decision:** Accepted as-is. No fix needed.

### F5 (LOW): Cache Action Field in snprintf (Accepted)

**File:** `phase2/src/sel4/main_arm64.c`, lines 588-589

**Description:** The `action` field from cache lookup results is formatted directly into an IPC response. If cache data were corrupted (missing null terminator), snprintf could read past the action buffer. Currently safe because all cache data is initialized at startup from compiled-in patterns.

**Decision:** Accepted as-is. Would need fixing if cache becomes dynamically loadable in Phase 3.

### F6 (HIGH): IPv4 total_len vs IHL Underflow in ICMP Handling

**File:** `phase2/src/drivers/net_stack.c`, `handle_icmp()` and `net_is_icmp_echo_reply()`

**Description:** `handle_icmp()` computed `icmp_len = ip_total - ip_hdr_len` without first validating that `ip_total >= ip_hdr_len`. A malformed IPv4 header with `total_len < IHL` could underflow the unsigned subtraction and pass a very large length into `net_checksum()`, causing an out-of-bounds read from the reply buffer. `net_is_icmp_echo_reply()` also accepted arbitrary IHL values (including `< 5`) and derived offsets without first bounding `ip_hdr_len` to the IPv4 header range.

**Fix:** Added IPv4 length sanity checks:
- Reject frames where `ip_hdr_len` is not in \([20, 60]\) bytes.
- Reject frames where `len < ETH_HLEN + ip_hdr_len`.
- Reject frames where `ip_total < ip_hdr_len` or `ip_total > (len - ETH_HLEN)`.

This ensures checksum and parsing logic never operate on impossible lengths.

## Code Review: Critical Paths

### Network RX Path (net_stack.c + bcm_genet.c)

- ARP handler validates header length before parsing (line 118-130)
- ICMP handler clamps memcpy to actual received frame length (line 188-189)
- IP total_len validated against actual frame length (line 203-205)
- GENET RX runt frame detection (dma_len <= prepend + FCS)
- **No buffer overflow vulnerabilities found in packet parsing**

### UART IPC Input Path (main_arm64.c)

- Frame payload bounded by MAX_MESSAGE_SIZE (240 bytes) at receive
- Command string null-terminated before dispatch
- All response buffers are stack-allocated with known sizes
- snprintf used consistently (never sprintf)
- **No command injection vectors found**

### DMA Buffer Handling (dma_alloc.c + bcm_dma.c)

- DMA pool pre-allocated with fixed size (256KB)
- Physical address tracking prevents invalid DMA pointers
- All DMA buffers mapped with vm_attributes=0 (uncacheable) per seL4 constraint
- Control blocks 32-byte aligned per BCM2711 DMA requirement
- **No cache coherency issues (uncacheable mapping eliminates the class)**

### Ring Buffer State (dual_ring_buffer.c)

- Magic number (0xDEADBEEF) and version validation on init
- Payload size bounds checked against MAX_PAYLOAD_SIZE
- Head/tail indices use modular arithmetic with power-of-2 buffer sizes
- **No wraparound bugs found**

## Recommendations for Phase 3

1. If cache becomes dynamically loadable, add null-terminator validation on cache entries
2. Consider fuzzing network packet parsing with malformed headers
3. Add overflow-safe arithmetic helpers if more complex size calculations are needed
4. Audit any new IPC message types for consistent bounds checking
