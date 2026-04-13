# Phase 3c Security Audit — Adversarial Code Review

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Adversarial security audit of all Phase 3 x86 code added since the March 22, 2026 audit (SEC-001 through SEC-026). Line-by-line review, data flow tracing, threat modeling. Fix all HIGH/CRITICAL. Document findings as SEC-027+.

**Architecture:** Two-pass audit. Pass 1: read every file, identify findings, write report. Pass 2: fix HIGH/CRITICAL findings, verify CI. Single agent per pass to maintain full context.

**Tech Stack:** Manual code review, no automated tools (the fuzz harness already covers runtime testing).

---

## Audit Scope

### New Files (not in March audit)

| File | LOC | Attack Surface |
|------|-----|----------------|
| `nvme.c/h` | ~380 | DMA queues, IDENTIFY parsing, sector reads |
| `fat32.c/h` | ~200 | BPB parsing, cluster chains, directory scan |
| `nic_i211.c/h` | ~350 | TX/RX descriptor rings, MMIO registers |
| `pci.c/h` | ~280 | Config space reads, BAR parsing, bus scan |
| `vga_text.c/h` | ~100 | Framebuffer writes |
| `fuzz_harness.c` | ~300 | (test code, not deployed) |

### Modified Files (changed since March audit)

| File | Changes | Risk |
|------|---------|------|
| `shmem_ipc.c/h` | Ring 16→15, CRC-32, div-by-zero fix | Re-verify SEC-001 |
| `main_x86.c` | NVMe init, FAT32 loading, workload loop, IPC drain | New entry points |
| `inference_server.c` | 3-path model loading, argv expansion | New argv parsing |
| `gguf_parser.c/h` | Verify SEC-003/007/008 fixes hold | Regression check |
| `gguf_vocab.c/h` | Raw binary walking | OOB read risk |
| `llama_quant.c/h` | Pointer arithmetic into .rodata | Bounds check |
| `tokenizer.c/h` | BPE hash table | Probe termination |

---

## Task 1: Audit — Read All Files and Document Findings

**Files to read (in order):**

1. `phase3/src/drivers/nvme.c` + `nvme.h`
2. `phase3/src/drivers/fat32.c` + `fat32.h`
3. `phase3/src/drivers/nic_i211.c` + `nic_i211.h`
4. `phase3/src/drivers/pci.c` + `pci.h`
5. `phase3/src/drivers/vga_text.c` + `vga_text.h`
6. `phase3/src/ipc/shmem_ipc.c` + `shmem_ipc.h`
7. `phase3/src/sel4/main_x86.c` (focus on NVMe init, FAT32, workload loop, IPC drain)
8. `phase3/src/sel4/inference_server.c`
9. `phase3/src/ai/gguf_parser.c` + `gguf_parser.h` (verify SEC-003/007/008)
10. `phase3/src/ai/gguf_vocab.c` + `gguf_vocab.h`
11. `phase3/src/ai/llama_quant.c` + `llama_quant.h`
12. `phase3/src/ai/tokenizer.c` + `tokenizer.h`

**For each file, check:**

- [ ] **Bounds checking:** Every `memcpy`, array index, pointer arithmetic
- [ ] **Integer overflow:** Multiplication, addition that could wrap
- [ ] **Input validation:** Untrusted data (network, disk, IPC) validated before use
- [ ] **Loop termination:** Every while/for loop has a bounded exit condition
- [ ] **Error handling:** Failures don't leave state inconsistent
- [ ] **TOCTOU:** Shared memory read once into local, not re-read
- [ ] **Buffer sizes:** Stack buffers large enough for worst case
- [ ] **Null termination:** Strings from external sources properly terminated

**Specific attack vectors to trace:**

1. **FAT32 cluster chain loop:** Can a circular cluster chain cause infinite loop in `fat32_read_file`?
2. **NVMe IDENTIFY parsing:** Can crafted IDENTIFY data cause OOB read in model/serial/LBA parsing?
3. **I211 RX descriptor:** Can a received frame with length > RX_BUF_SIZE cause overflow?
4. **PCI BAR validation:** Can a 64-bit BAR with upper bits set cause address space issues?
5. **VGA scroll:** Can rapid output cause row/col to go out of 80×25 bounds?
6. **IPC drain loop:** Can Process B send infinite MSG_RESPONSE to make Process A spin?
7. **gguf_vocab raw walk:** Can crafted token data cause read past model buffer?
8. **tokenizer hash probe:** Can all tokens hash to same bucket, causing O(n²)?
9. **Workload PRNG:** Does xorshift32 with seed 42 ever produce a stuck state?

**Deliverable:** Write `phase3/docs/SECURITY_AUDIT_2026-04-06.md` with:
- Threat model (updated for x86 bare metal)
- Data flow diagram (NVMe + I211 + FAT32 entry points)
- Findings table (SEC-027+) with severity, CWE, file:line
- Detailed description of each finding
- Fix status (FIXED with commit hash, or ACCEPTED with rationale)

- [ ] **Step 1: Read all 12 file pairs**
- [ ] **Step 2: Document findings in audit report**
- [ ] **Step 3: Commit report**

```bash
git add phase3/docs/SECURITY_AUDIT_2026-04-06.md
git commit -m "docs: Phase 3c security audit report — N findings (X HIGH, Y MED, Z LOW)"
```

---

## Task 2: Fix HIGH/CRITICAL Findings

For each HIGH or CRITICAL finding from Task 1:

- [ ] **Step 1: Implement fix** (targeted edit to the specific file:line)
- [ ] **Step 2: Verify fix doesn't break existing tests**

```bash
# Run all affected test suites
wsl bash -lc "cd /mnt/c/Users/jluca/Documents/JARVIS_OS && \
  gcc ... test_nvme.c nvme.c -o /tmp/t && /tmp/t && \
  gcc ... test_fat32.c fat32.c -o /tmp/t && /tmp/t && \
  gcc ... test_shmem_ipc.c shmem_ipc.c -o /tmp/t && /tmp/t && \
  gcc ... fuzz_harness.c ... -o /tmp/t && /tmp/t"
```

- [ ] **Step 3: Commit each fix**

```bash
git commit -m "security: fix SEC-0XX — description"
```

---

## Task 3: Update CLAUDE.md

- [ ] **Step 1: Add milestone**

```
| Phase 3c security audit: N findings (X HIGH, Y MED, Z LOW, W INFO) | DONE |
| All HIGH/CRITICAL findings fixed | DONE |
```

- [ ] **Step 2: Update "What remains" / "Next"**

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: Phase 3c security audit milestone — N findings, all HIGH fixed"
```

---

## Task 4: Push and Verify CI

- [ ] **Step 1: Push all commits**
- [ ] **Step 2: Verify CI green**

```bash
gh run list --limit 1
```

---

## Effort Estimate

| Task | Hours | Notes |
|------|-------|-------|
| Task 1: Audit (read + report) | 4-6 | 12 file pairs, ~2500 LOC |
| Task 2: Fix findings | 2-4 | Depends on count |
| Task 3: CLAUDE.md | 0.25 | |
| Task 4: Push + verify | 0.25 | |
| **Total** | **7-11** | ~1-2 sessions |

## Key Constraint

The audit agent needs to read ALL files in scope. This is a large context task — use Opus 4.6 with 1M context. Do NOT use a smaller model. The audit must be thorough, not superficial.
