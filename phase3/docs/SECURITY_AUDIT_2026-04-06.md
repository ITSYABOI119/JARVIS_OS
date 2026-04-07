# JARVIS AI-OS — Phase 3c Security Audit Report

**Date:** April 6, 2026
**Auditor:** Claude Opus 4.6 (automated, adversarial review)
**Scope:** Phase 3 x86 code added since March 22, 2026 audit (~3000 LOC, 12 file pairs)
**Previous Audit:** SEC-001 through SEC-026 (March 22, 2026) — all fixed

**Status:** 11/14 findings resolved. 3 INFO-level findings accepted by design.

| Severity | Total | Fixed | Accepted |
|----------|-------|-------|----------|
| HIGH     | 4     | 4     | 0        |
| MEDIUM   | 3     | 3     | 0        |
| LOW      | 4     | 4     | 0        |
| INFO     | 3     | 0     | 3        |
| **Total** | **14** | **11** | **3**  |

**Fix commits:**
- `13fc749` — SEC-027 through SEC-033, SEC-036 (8 fixes: fat32, nvme, nic_i211, vga, tokenizer)
- `8370245` — SEC-034, SEC-035, SEC-037 (3 fixes: NVMe CID wrap, FAT32 cluster overflow, uint64 stats counters)

---

## Threat Model

| Attacker | Access | Goal | Example |
|----------|--------|------|---------|
| **NVMe storage** | Crafted disk sectors | Parser exploitation, infinite loops, OOB read | Malformed FAT32 BPB, circular cluster chains, crafted IDENTIFY data |
| **Network (I211)** | Ethernet frames via I211-AT NIC | OOB read/write, DoS | Malicious RX descriptor length |
| **IPC peer** | Shared memory between Process A/B | Infinite drain loop, stale-data TOCTOU | Crafted shmem_msg_t, infinite MSG_RESPONSE |
| **GGUF model file** | Crafted model on NVMe FAT32 | OOB read, heap corruption, infinite loops | Malformed GGUF vocab, huge array counts |
| **PCI bus** | Crafted config space responses | Truncated BAR addresses, wrong MMIO range | Malicious BAR type bits |
| **VGA output** | Crafted input strings | Framebuffer OOB write | Tab at column 78 |

## Data Flow Diagram

```
                    UNTRUSTED ENTRY POINTS (Phase 3c x86)
                    ══════════════════════════════════════
  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌───────────┐   ┌──────────┐
  │ NVMe     │   │ I211 NIC │   │ Shared   │   │ GGUF from │   │ PCI      │
  │ sectors  │   │ RX DMA   │   │ Memory   │   │ FAT32     │   │ config   │
  └────┬─────┘   └────┬─────┘   └────┬─────┘   └─────┬─────┘   └────┬─────┘
       │              │              │                │              │
       ▼              ▼              ▼                ▼              ▼
  fat32_init     i211_nic_recv  shmem_ipc_recv   gguf_open_memory  pci_scan
  fat32_read     → memcpy      → msg dispatch    gguf_vocab_      pci_get_
  _file          from rx_buf   → drain loop      extract          bar_address
       │              │              │                │              │
       ▼              ▼              ▼                ▼              ▼
  cluster chain   frame to       query →         vocab array     BAR addr →
  → model load    caller buf     Process B       → tokenizer     MMIO map
```

---

## Findings Summary

| ID | Severity | CWE | File:Line | Title | Attacker | Fixed |
|----|----------|-----|-----------|-------|----------|-------|
| SEC-027 | **HIGH** | CWE-835 | fat32.c:122,174 | FAT32 cluster chain infinite loop on circular FAT | NVMe storage | YES |
| SEC-028 | **HIGH** | CWE-20 | fat32.c:103-108 | FAT32 BPB fields not validated (div-by-zero, overflow) | NVMe storage | YES |
| SEC-029 | **HIGH** | CWE-125 | fat32.c:59-66 | fat32_next_cluster stack buffer OOB read (sector > 512B) | NVMe storage | YES |
| SEC-030 | **MEDIUM** | CWE-20 | nvme.c:351-358 | NVMe IDENTIFY LBADS not range-checked (insane block_size) | NVMe storage | YES |
| SEC-031 | **MEDIUM** | CWE-369 | nvme.c:398 | nvme_read_sectors div-by-zero if ns_block_size == 0 | NVMe storage | YES |
| SEC-032 | **LOW** | CWE-787 | vga_text.c:49 | VGA tab handler can set col > VGA_WIDTH before check | VGA output | YES |
| SEC-033 | **HIGH** | CWE-125 | nic_i211.c:320-324 | I211 RX descriptor length not clamped to DMA buffer size | Network | YES |
| SEC-034 | **LOW** | CWE-190 | nvme.c:99 | NVMe CID counter uint16_t wraps at 65536 (benign but non-spec) | NVMe storage | YES — skip CID 0 on wrap |
| SEC-035 | **LOW** | CWE-190 | fat32.c:57 | fat32_next_cluster: cluster*4 overflows for cluster > 1B | NVMe storage | YES — bounds check before multiply |
| SEC-036 | **MEDIUM** | CWE-835 | tokenizer.c:73-79 | tokenizer_find hash probe has no iteration bound | GGUF model | YES |
| SEC-037 | **LOW** | CWE-190 | main_x86.c:1432 | q_total uint32_t wraps at ~4.3B queries (benign for 30-day test) | Internal | YES — upgraded to uint64_t |
| SEC-038 | **INFO** | CWE-327 | shmem_ipc.c:15-25 | CRC-32 is integrity-only, not authentication | IPC peer | No (by design) |
| SEC-039 | **INFO** | CWE-693 | inference_server.c:317-320 | SHIELD_CHECK always returns ALLOW (stub) | IPC peer | No (future work) |
| SEC-040 | **INFO** | CWE-404 | gguf_vocab.c:108 | skip_value on very large GGUF string array is O(n) | GGUF model | No (accepted) |

---

## Detailed Findings

### SEC-027: FAT32 cluster chain infinite loop on circular FAT
**Severity:** HIGH
**CWE:** CWE-835 (Loop with Unreachable Exit Condition)
**File(s):** `phase3/src/drivers/fat32.c` lines 122, 174
**Attacker:** NVMe storage (crafted FAT32 partition)

**Description:** Both `fat32_find_file` and `fat32_read_file` follow the FAT cluster chain in a `while (cluster >= 2 && cluster < FAT32_EOC_MIN)` loop. A crafted FAT table with a circular chain (e.g., cluster 100 -> 101 -> 100) would cause an infinite loop, permanently hanging the rootserver.

**Root cause:** No iteration bound on cluster chain traversal.

**Impact:** Denial of service. The seL4 rootserver hangs during boot, requiring a hard reset.

**Fix:** Added `max_clusters` counter (1,000,000) to both functions. After 1M clusters (32GB at 32KB/cluster), the loop terminates with an error. (commit `13fc749`)

---

### SEC-028: FAT32 BPB fields not validated
**Severity:** HIGH
**CWE:** CWE-20 (Improper Input Validation)
**File(s):** `phase3/src/drivers/fat32.c` lines 103-113
**Attacker:** NVMe storage (crafted FAT32 partition)

**Description:** `fat32_init` reads BPB fields from disk and uses them directly in arithmetic: `bytes_per_sector` is a divisor in `fat32_next_cluster` (div-by-zero if 0), `sectors_per_cluster * bytes_per_sector` can overflow `uint32_t` with crafted values, and `root_cluster < 2` causes `cluster_to_lba` to underflow via `(cluster - 2)`.

**Root cause:** BPB fields from an untrusted disk sector are used without validation.

**Impact:** Div-by-zero crash, integer underflow causing wildcard LBA reads, or integer overflow corrupting cluster math.

**Fix:** Added validation: bytes_per_sector must be 512/1024/2048/4096, sectors_per_cluster != 0, num_fats != 0, fat_size_sectors != 0, root_cluster >= 2. (commit `13fc749`)

---

### SEC-029: fat32_next_cluster stack buffer OOB read
**Severity:** HIGH
**CWE:** CWE-125 (Out-of-bounds Read)
**File(s):** `phase3/src/drivers/fat32.c` lines 57-68
**Attacker:** NVMe storage (crafted BPB with bytes_per_sector > 512)

**Description:** `fat32_next_cluster` uses a fixed `uint8_t sector_buf[512]` but computes `entry_offset = fat_offset % fs->bytes_per_sector`. If `bytes_per_sector` is 1024, 2048, or 4096 (all valid FAT32 values), then `entry_offset` can be up to 4092, but `read_le32(sector_buf + entry_offset)` reads past the 512-byte stack buffer.

**Root cause:** Stack buffer size is hardcoded to 512 but the modulus is based on the BPB's `bytes_per_sector`.

**Impact:** Stack buffer OOB read. On bare-metal seL4, this reads garbage from the stack; on a system with stack canaries, it could crash.

**Fix:** Added bounds check: if `entry_offset + 4 > sizeof(sector_buf)`, return error. Note: with SEC-028's validation that bytes_per_sector is 512/1024/2048/4096, this only triggers when bytes_per_sector > 512. A future improvement could use a larger buffer or read multiple sectors. (commit `13fc749`)

---

### SEC-030: NVMe IDENTIFY LBADS not range-checked
**Severity:** MEDIUM
**CWE:** CWE-20 (Improper Input Validation)
**File(s):** `phase3/src/drivers/nvme.c` lines 351-358
**Attacker:** NVMe storage (crafted IDENTIFY namespace response)

**Description:** The LBADS field (log2 of block size) is read from byte 128+ of the IDENTIFY namespace response. A crafted value of LBADS=31 would compute `ns_block_size = 1U << 31 = 2147483648`, causing `nvme_read_sectors` to compute `max_sectors = 8192 / 2147483648 = 0`, making all reads fail. A value of LBADS=0 would compute `ns_block_size = 512` (fallback), which is fine, but any value outside the valid range (9-12 for 512-4096 bytes) is incorrect.

**Root cause:** Untrusted LBADS value used directly in shift without range validation.

**Impact:** Denial of service (all sector reads fail) or incorrect block size math.

**Fix:** Clamped LBADS to valid range 9-12; values outside this range default to 512. (commit `13fc749`)

---

### SEC-031: nvme_read_sectors div-by-zero if ns_block_size == 0
**Severity:** MEDIUM
**CWE:** CWE-369 (Divide By Zero)
**File(s):** `phase3/src/drivers/nvme.c` line 398
**Attacker:** NVMe storage (incomplete init)

**Description:** If `nvme_init` fails partway through (e.g., IDENTIFY namespace fails after controller enable), `ns_block_size` may remain 0 from the initial memset. If `nvme_read_sectors` is called on a partially-initialized controller, `8192 / ctrl->ns_block_size` causes a div-by-zero.

**Root cause:** No validation of `ns_block_size` before division.

**Impact:** Crash via hardware division exception on bare-metal.

**Fix:** Added `ns_block_size == 0` check at start of `nvme_read_sectors`. (commit `13fc749`)

---

### SEC-032: VGA tab handler framebuffer write risk
**Severity:** LOW
**CWE:** CWE-787 (Out-of-bounds Write)
**File(s):** `phase3/src/drivers/vga_text.c` line 49
**Attacker:** Any string output containing tabs

**Description:** The tab handler computes `vga_col = (vga_col + 8) & ~7`, which can produce values up to 80 when `vga_col` is 73-79. Although the generic `if (vga_col >= VGA_WIDTH)` check at the bottom of `vga_putc` catches this before the NEXT call, there is no write between the tab handler and that check, so no actual OOB occurs. The added explicit check is defense-in-depth.

**Root cause:** Tab arithmetic can exceed VGA_WIDTH.

**Impact:** No actual OOB write with current code flow, but fragile if refactored. Defense-in-depth fix applied.

**Fix:** Added explicit bounds check after tab col update. (commit `13fc749`)

---

### SEC-033: I211 RX descriptor length not clamped to DMA buffer size
**Severity:** HIGH
**CWE:** CWE-125 (Out-of-bounds Read)
**File(s):** `phase3/src/drivers/nic_i211.c` lines 320-324
**Attacker:** Network (malicious/buggy NIC hardware, or DMA corruption)

**Description:** `i211_nic_recv` reads `frame_len = desc->length` from a hardware-written RX descriptor. This is clamped to `max_len` (caller's buffer), but NOT to `I211_RX_BUF_SIZE` (2048 — the DMA buffer size). If a buggy or malicious NIC reports `length = 65535` in the descriptor, and the caller passes `max_len = 65535`, the `memcpy(buf, nic->rx_bufs[idx], frame_len)` reads 65535 bytes from a 2048-byte DMA buffer — a 63KB OOB read.

**Root cause:** Only caller's buffer size is checked, not the source DMA buffer size.

**Impact:** Out-of-bounds read from DMA buffer. On bare-metal seL4, this reads adjacent memory (potentially other DMA buffers or kernel data).

**Fix:** Added clamp `if (frame_len > I211_RX_BUF_SIZE) frame_len = I211_RX_BUF_SIZE` before the existing `max_len` clamp. (commit `13fc749`)

---

### SEC-034: NVMe CID counter wraps at uint16_t max
**Severity:** LOW
**CWE:** CWE-190 (Integer Overflow)
**File(s):** `phase3/src/drivers/nvme.c` line 99
**Attacker:** Long-running operation

**Description:** `cmd->cid = q->cid_counter++` uses a `uint16_t` that wraps at 65536. NVMe spec allows CID reuse as long as no two outstanding commands share a CID. Since the driver uses synchronous polled I/O (only one command outstanding at a time), wrap is benign. However, some controllers treat CID 0 as invalid, so skipping 0 on wrap is safer.

**Fix:** Changed CID increment to `q->cid_counter = (q->cid_counter % 65535) + 1`, which cycles through 1..65535 and never hits 0. (commit `8370245`)

---

### SEC-035: fat32_next_cluster cluster*4 overflow for large clusters
**Severity:** LOW
**CWE:** CWE-190 (Integer Overflow)
**File(s):** `phase3/src/drivers/fat32.c` line 57
**Attacker:** NVMe storage (crafted FAT with cluster numbers > 1 billion)

**Description:** `uint32_t fat_offset = cluster * 4` overflows if `cluster > 0x3FFFFFFF` (~1.07 billion). FAT32 uses 28-bit cluster numbers (max ~268 million), so valid FAT32 images cannot trigger this. However, defense-in-depth: a bounds check before the multiplication eliminates the entire class of overflow regardless of caller assumptions.

**Fix:** Added explicit guard: `if (cluster > 0x0FFFFFFF) { *next = FAT32_EOC_MIN; return 0; }` before the multiplication. (commit `8370245`)

---

### SEC-036: tokenizer_find hash probe has no iteration bound
**Severity:** MEDIUM
**CWE:** CWE-835 (Loop with Unreachable Exit Condition)
**File(s):** `phase3/src/ai/tokenizer.c` lines 73-79
**Attacker:** Crafted GGUF model with adversarial token strings

**Description:** `tokenizer_find` uses open-addressing linear probe: `while (t->ht_table[h] != -1)`. The hash table is sized to 2x vocab_size, so ~50% slots are empty and the loop normally terminates quickly. However, if a malicious model provides tokens that all hash to the same bucket, or if the table is somehow full, the loop becomes O(n) per lookup. With 128K tokens and BPE merge loop calling `tokenizer_find` O(n^2) times, this could cause extreme latency.

More critically, if `ht_capacity` were somehow corrupted to be much smaller than `vocab_size`, the table could be 100% full, making the probe loop infinite.

**Root cause:** No upper bound on linear probe iterations.

**Impact:** Potential infinite loop or extreme latency during tokenization.

**Fix:** Added `probes` counter bounded to `ht_capacity` iterations. (commit `13fc749`)

---

### SEC-037: q_total uint32_t wraps at ~4.3B queries
**Severity:** LOW
**CWE:** CWE-190 (Integer Overflow)
**File(s):** `phase3/src/sel4/main_x86.c` line 1432
**Attacker:** Internal (long-running stability test)

**Description:** `q_total` is `uint32_t`, wrapping at 4,294,967,296. At the observed query rate (~10 queries/sec with inference), the 30-day stability test would generate ~26M queries — well within range. Even at 1000 queries/sec, wrap occurs at ~50 days. For multi-year uptime scenarios, this becomes a real limit.

**Fix:** Upgraded `q_total`, `q_hits`, `q_infer`, `q_heartbeat`, `q_shield`, `q_errors` from `uint32_t` to `uint64_t`. Eliminates any practical wrap concern (584 years at 1B queries/sec). (commit `8370245`)

---

### SEC-038: CRC-32 is integrity-only, not authentication
**Severity:** INFO
**CWE:** CWE-327 (Use of Broken or Risky Cryptographic Algorithm)
**File(s):** `phase3/src/ipc/shmem_ipc.c` lines 15-25
**Attacker:** Compromised Process B

**Description:** The CRC-32 in shmem IPC (SEC-020 fix) detects accidental corruption but provides no authentication. A malicious Process B can compute valid CRC-32 values for crafted messages.

**Status:** Accepted — by design. Both processes run in the same trust domain (both are JARVIS code). CRC-32 protects against bit-flips and partial writes, not adversarial attacks.

---

### SEC-039: SHIELD_CHECK always returns ALLOW in Process B
**Severity:** INFO
**CWE:** CWE-693 (Protection Mechanism Failure)
**File(s):** `phase3/src/sel4/inference_server.c` lines 317-320

**Description:** The SHIELD_CHECK handler in Process B returns a hardcoded 0 (SHIELD_ALLOW) for all queries. The comment says "full model-assisted SHIELD is future work." Process A does its own keyword-based SHIELD check for the workload loop, so this is not a bypass — it's an unimplemented feature in Process B.

**Status:** Accepted — documented as future work (Phase 3c).

---

### SEC-040: gguf_vocab skip_value is O(n) for large string arrays
**Severity:** INFO
**CWE:** CWE-404 (Improper Resource Shutdown or Release)
**File(s):** `phase3/src/ai/gguf_vocab.c` line 108

**Description:** When `skip_value` encounters a string array of N elements, it reads each string's length prefix and skips its bytes — O(N) I/O operations. For a crafted GGUF with a string array of 1 million elements, each with a 1-byte string, this performs 2M read operations (length + skip per string). While bounded (N capped at `GGUF_MAX_KV_PAIRS = 65536` in the caller), this could cause multi-second delays during model loading.

**Status:** Accepted — bounded by existing caps. Model loading is a one-time operation.

---

## March Audit Regression Check

All 26 findings from the March 22, 2026 audit were verified to remain fixed:

| ID | Status | Verification |
|----|--------|-------------|
| SEC-001 | Still fixed | `shmem_ipc_recv` reads `local_length` from slot once, validates before use (shmem_ipc.c:124-129) |
| SEC-002 | Still fixed | Phase 2 code unchanged; trust_level parsing uses bounded enum check |
| SEC-003 | Still fixed | `read_tensor_info` checks `n_elements > UINT64_MAX / dims[i]` (gguf_parser.c:288-289) |
| SEC-004 | Still fixed | Phase 2 net_stack unchanged |
| SEC-005 | Still fixed | Phase 2 fdt_parser unchanged |
| SEC-006 | Still fixed | Phase 2 net_stack unchanged |
| SEC-007 | Still fixed | `skip_gguf_value` has `depth > GGUF_MAX_ARRAY_DEPTH` check (gguf_parser.c:194) |
| SEC-008 | Still fixed | `n_tensors > GGUF_MAX_TENSORS` and `n_kv > GGUF_MAX_KV_PAIRS` checks (gguf_parser.c:337-338) |
| SEC-009 | Still fixed | Phase 2 UART code unchanged |
| SEC-010 | Still fixed | `normalize_for_shield` in main_x86.c:233-249, keyword matching uses normalized input |
| SEC-011 | Still fixed | CMakeLists.txt flags unchanged |
| SEC-012 | Still fixed | Architectural — Phase 3 uses shmem IPC, not plaintext UART |
| SEC-013 | Still fixed | Architectural — shmem has CRC-32 integrity (SEC-020 fix) |
| SEC-014 | **Improved** | Process isolation implemented — Process A (rootserver) and Process B (inference) in separate seL4 processes with capability separation |
| SEC-015 | Still fixed | Phase 2 drivers unchanged |
| SEC-016 | Still fixed | Phase 2 IPC code unchanged |
| SEC-017 | Still fixed | `skip_bytes` uses chunked `fseek` with LONG_MAX guard (gguf_parser.c:81-101) |
| SEC-018 | Still fixed | Phase 2 RNG code unchanged |
| SEC-019 | Still fixed | Phase 2 FDT code unchanged |
| SEC-020 | **Active** | CRC-32 on every IPC message (shmem_ipc.c:89-90, verified in recv at line 133-135) |
| SEC-021 | Still fixed | Phase 2 USB code unchanged |
| SEC-022 | Still fixed | Phase 2 EMMC code unchanged |
| SEC-023 | Still fixed | Phase 2 dual_ring_buffer unchanged |
| SEC-024 | Still fixed | Phase 2 decision_cache unchanged |
| SEC-025 | Still fixed | Phase 2 net_stack unchanged |
| SEC-026 | Still fixed | Phase 2 dual_ring_buffer unchanged |

---

## Additional Observations (Not Findings)

### Positive security patterns observed

1. **Process isolation (SEC-014 improvement):** Process B runs in a separate seL4 process with its own VSpace. Shared memory is explicitly mapped via capability duplication. A crash in Process B cannot corrupt Process A's state.

2. **Zero-copy weight pointers are safe:** `llama_quant.c` stores raw pointers into GGUF .rodata. Since the model is loaded via `fat32_read_file` into pre-allocated frames before pointer resolution, and frames are mapped read-only by seL4, there is no TOCTOU risk for weight data.

3. **GGUF parser has comprehensive bounds checks:** Tensor element overflow (SEC-003), recursion depth (SEC-007), allocation limits (SEC-008), and `skip_bytes` portability (SEC-017) were all fixed in the March audit and remain intact.

4. **NVMe driver is synchronous:** Single-command polled I/O eliminates race conditions in queue management. The CSTS readback after doorbell writes ensures MMIO write ordering.

5. **VGA framebuffer is bounds-safe:** `vga_scroll` and `vga_clear` iterate exactly `VGA_WIDTH * VGA_HEIGHT` elements. `vga_putc` checks `vga_row >= VGA_HEIGHT` after every character.

---

## Summary

| Severity | Count | Fixed | Accepted |
|----------|-------|-------|----------|
| HIGH | 4 | 4 | 0 |
| MEDIUM | 3 | 3 | 0 |
| LOW | 4 | 1 | 3 |
| INFO | 3 | 0 | 3 |
| **Total** | **14** | **8** | **6** |

All HIGH and MEDIUM findings have been fixed. The 6 accepted findings are either benign (integer wraps within operational bounds), by-design (CRC-32 is integrity not auth), or documented future work (SHIELD stub).
