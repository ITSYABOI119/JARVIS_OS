# Dynamic Model Scaling — Hot-Swap Models Based on Workload

**Date:** 2026-04-16
**Status:** Approved
**Scope:** Phase 3c — wire existing `model_scaling.c` state machine to real model hot-swap via NVMe + IPC

---

## 1. Problem

The model scaler (`model_scaling.c/h`) has 4 states (IDLE → ACTIVE → CRITICAL → EMERGENCY) with hysteresis, but it's not connected to anything. No model loading, no hot-swap, no cache miss tracking. The system always runs one model regardless of workload.

## 2. Solution

PA owns scaling decisions and NVMe I/O. PB owns inference. The scaler runs in PA's workload loop, evaluating cache miss rate over a rolling 100-query window. When the scaler transitions state, PA orchestrates a model swap: PB unloads, PA reads the new model from NVMe FAT32 into shared pages, PB reloads. During the swap window (~30-60s), PA serves cache hits only (85%+ hit rate keeps the system responsive).

## 3. Tier Configuration

| State | Model | FAT32 Filename | Size | tok/s (16T) | Quality |
|-------|-------|---------------|------|-------------|---------|
| IDLE | Llama 3.2 1B Q4_K_M | `LLAMA1B.GUF` | 771MB | 19.79 | 5.37/10 |
| ACTIVE | Gemma 4 E2B Q4_K_M | `GEMMA2B.GUF` | 2.9GB | 8.63 | 8.40/10 |
| CRITICAL | Mistral 7B Q8_0 | `MISTR7B.GUF` | 7.2GB | 4.16 | 7.50/10 |
| EMERGENCY | Cache + SHIELD rules | (none) | 0 | <1ms | rules |

All three GGUF files stored on the NVMe FAT32 partition (same partition as current `MODEL.GUF`). Only one model loaded in RAM at a time. FAT32 8.3 naming required (no long filenames).

## 4. Scaling Signal

PA tracks cache misses in a rolling window of the last 100 queries.

```
miss_rate = cache_misses_in_window / window_size
```

Fed into existing scaler: `scaler_update(&scaler, miss_rate, pending_queries)`.

Existing thresholds (from `model_scaling.c`):
- `idle_threshold = 0.2` — below 20% miss rate for 10 consecutive evaluations → de-escalate to IDLE
- `active_threshold = 0.5` — above 50% miss rate → escalate to ACTIVE
- `critical_threshold = 0.8` — above 80% miss rate → escalate to CRITICAL

SHIELD can force EMERGENCY instantly via existing `scaler_force_emergency()`.

**Scaler evaluation frequency:** Once per query, after the cache lookup / inference response. The rolling window naturally smooths per-query noise.

**Scaler frozen during swap:** While `swap_in_progress` is true, skip `scaler_update()` calls. Resume after swap completes.

## 5. IPC Protocol

New message type `MSG_MODEL_SWAP (0x10)` in `shmem_ipc.h`. Payload carries a 1-byte command and optional 4-byte model size (little-endian).

| Command | Value | Direction | Payload | Meaning |
|---------|-------|-----------|---------|---------|
| `SWAP_UNLOAD` | 0x01 | PA → PB | (none) | Free current model + state |
| `SWAP_UNLOADED` | 0x02 | PB → PA | (none) | Model freed, ready for new data |
| `SWAP_LOAD` | 0x03 | PA → PB | uint32 model_size | Parse + load model from shared pages |
| `SWAP_LOADED` | 0x04 | PB → PA | (none) | Model ready, resume queries |
| `SWAP_FAILED` | 0x05 | PB → PA | (none) | Load failed, stay cache-only |

## 6. Swap Sequence

```
1. PA: scaler_update() returns new state (e.g., IDLE → ACTIVE)
2. PA: if new_state == current_model_state → no swap needed, continue
3. PA: set swap_in_progress = true, log "Scaling: IDLE → ACTIVE"
4. PA: send MSG_MODEL_SWAP(SWAP_UNLOAD) to PB via response ring, signal
5. PA: poll/yield wait for PB response (same pattern as inference wait)
6. PB: receive SWAP_UNLOAD in IPC loop
7. PB: qmodel_free(&qm), llama_free_state(&state), gguf_close(&ctx)
8. PB: send MSG_MODEL_SWAP(SWAP_UNLOADED), signal PA
9. PA: receive SWAP_UNLOADED
10. PA: look up new model filename via scaler_model_file(new_state)
11. PA: fat32_find_file(filename) → fat32_read_file into shared model pages
12. PA: send MSG_MODEL_SWAP(SWAP_LOAD, model_size), signal PB
13. PB: receive SWAP_LOAD with size
14. PB: gguf_open_memory(model_data, size) → qmodel_load → gguf_vocab_extract
       → gguf_vocab_init_tokenizer → llama_alloc_state
15. PB: run model probe (page sweep + single forward pass, existing code)
16. PB: send MSG_MODEL_SWAP(SWAP_LOADED), signal PA
17. PA: receive SWAP_LOADED → set swap_in_progress = false
18. PA: update current_model_state, log "Swap complete: now ACTIVE"
19. PA: resume normal workload loop
```

**During swap (steps 4-17):** PA's workload loop continues running. Cache hits are served normally (<1ms). Cache misses skip inference (PB is unavailable) — PA returns a fallback "model loading" response or simply skips the query. The miss counter still increments but the scaler is frozen.

**Swap timing estimates:**
- IDLE → ACTIVE (771MB → 2.9GB): ~2.9GB at ~50MB/s NVMe = ~58s + PB reload ~2s = ~60s
- ACTIVE → CRITICAL (2.9GB → 7.2GB): ~7.2GB / 50MB/s = ~144s + reload = ~150s
- Any → IDLE (→ 771MB): ~15s + reload = ~17s
- Any → EMERGENCY: instant (no model to load)

## 7. Model File Management

### NVMe FAT32 Layout

Current: single `MODEL.GUF` on the ESP partition (or whole-disk FAT32 in QEMU).

New: three files on the same partition:
```
/LLAMA1B.GUF    (771 MB)
/GEMMA2B.GUF    (2.9 GB)
/MISTR7B.GUF    (7.2 GB)
```

Total: ~10.9 GB on the FAT32 partition. The Lexar NM790 2TB has a ~4GB ESP — this doesn't fit. Options:
- **Resize ESP to 16GB** (one-time fdisk operation)
- **Use a second FAT32 partition** dedicated to models
- **Use whole-disk FAT32** (only for QEMU testing)

For bare metal: resize ESP or add a second partition. For QEMU: whole-disk FAT32 image (~12GB).

### File Lookup

New function in `model_scaling.h`:
```c
const char *scaler_model_file(scaling_state_t state);
```

Returns FAT32 8.3 filename:
- IDLE → `"LLAMA1B GUF"` (FAT32 space-padded 8.3 format)
- ACTIVE → `"GEMMA2B GUF"`
- CRITICAL → `"MISTR7B GUF"`
- EMERGENCY → NULL (no model)

### Shared Page Reuse

PA already allocates `MODEL_MAX_PAGES` (800K pages = 3.2GB) of frames for model data and maps them into PB's VSpace. The largest model (Mistral 7B Q8_0 at 7.2GB) exceeds this.

Fix: increase `MODEL_MAX_PAGES` to 2M pages (8GB) to fit Mistral 7B. Memory budget: 8GB model + 32MB KV cache + 128KB scratch = ~8.2GB. With 32GB RAM, this leaves ~24GB for OS + PA + caches.

When swapping to a smaller model, PA reads fewer pages — the extra pages are allocated but unused (zero cost since they're not accessed).

## 8. Process B Hot-Reload

PB's main IPC loop currently handles MSG_QUERY, MSG_HEARTBEAT, MSG_SHIELD_CHECK. Add MSG_MODEL_SWAP handling:

```c
case MSG_MODEL_SWAP: {
    uint8_t cmd = payload[0];
    if (cmd == SWAP_UNLOAD) {
        /* Free current model and state */
        llama_free_state(&state);
        tokenizer_free(&tok);
        gguf_vocab_free(&vocab);
        qmodel_free(&qm);
        gguf_close(&gguf_ctx);
        model_loaded = false;
        shmem_ipc_send(response_ring, MSG_MODEL_SWAP, msg_seq,
                       &(uint8_t){SWAP_UNLOADED}, 1);
        seL4_Signal(resp_notif);
    } else if (cmd == SWAP_LOAD) {
        uint32_t new_size;
        memcpy(&new_size, payload + 1, 4);
        /* Re-parse and load from same shared memory region */
        int err = gguf_open_memory(&gguf_ctx, model_data, new_size);
        if (!err) err = qmodel_load(&qm, &gguf_ctx, model_data);
        /* ... vocab, tokenizer, state alloc, probe ... */
        if (!err) {
            model_loaded = true;
            shmem_ipc_send(response_ring, MSG_MODEL_SWAP, msg_seq,
                           &(uint8_t){SWAP_LOADED}, 1);
        } else {
            shmem_ipc_send(response_ring, MSG_MODEL_SWAP, msg_seq,
                           &(uint8_t){SWAP_FAILED}, 1);
        }
        seL4_Signal(resp_notif);
    }
    break;
}
```

PB must track `model_loaded` state. If `model_loaded == false` and a MSG_QUERY arrives, PB sends an empty response (or a "no model" indicator).

## 9. Process A Workload Loop Changes

### Cache Miss Tracking

Add a rolling window to PA's workload loop:
```c
#define MISS_WINDOW 100
int miss_history[MISS_WINDOW];  /* 0 = hit, 1 = miss */
int miss_idx = 0;
int miss_count = 0;
int total_in_window = 0;
```

After each query: record hit/miss, compute `miss_rate = miss_count / total_in_window`.

### Scaler Integration

After each query (outside the swap window):
```c
if (!swap_in_progress) {
    float miss_rate = (total_in_window > 0) ? (float)miss_count / total_in_window : 0.0f;
    scaling_state_t new_state = scaler_update(&scaler, miss_rate, 0);
    if (new_state != current_model_state && new_state != SCALING_EMERGENCY) {
        initiate_swap(new_state);
    } else if (new_state == SCALING_EMERGENCY) {
        swap_in_progress = false;
        current_model_state = SCALING_EMERGENCY;
        /* PB stays idle, cache-only mode */
    }
}
```

### Swap Orchestration

`initiate_swap(new_state)`:
1. Set `swap_in_progress = true`
2. Send SWAP_UNLOAD to PB, wait for SWAP_UNLOADED
3. Read new model from NVMe FAT32 into shared pages
4. Send SWAP_LOAD to PB, wait for SWAP_LOADED or SWAP_FAILED
5. On success: update `current_model_state`, clear `swap_in_progress`
6. On failure: try to reload previous model (one retry), else EMERGENCY

## 10. Failure Handling

| Failure | Response |
|---------|----------|
| PB sends SWAP_FAILED | Log error. Try to reload PREVIOUS model. If that fails, force EMERGENCY. |
| PB timeout (60s) during swap | Assume PB crashed. Force EMERGENCY. Log for NVMe diagnostics. |
| NVMe read failure | Stay on current tier. Log error. Retry after 100 more queries. |
| Model file not found on FAT32 | Stay on current tier. Log missing file. |
| New model exceeds available pages | SWAP_FAILED from PB (alloc fails). Fall back to smaller model. |

## 11. Files to Modify

| File | Change | LOC Est. |
|------|--------|----------|
| `shmem_ipc.h` | Add MSG_MODEL_SWAP + command constants | ~10 |
| `model_scaling.h` | Update tier comments, add `scaler_model_file()` | ~15 |
| `model_scaling.c` | Implement `scaler_model_file()`, update tier comments | ~20 |
| `main_x86.c` | Cache miss tracking, scaler integration, swap orchestration | ~150 |
| `inference_server.c` | MSG_MODEL_SWAP handler (unload/reload), model_loaded flag | ~80 |
| `main_x86.c` (defines) | Increase MODEL_MAX_PAGES to 2M | ~1 |
| **Total** | | **~276** |

## 12. Testing

### Unit Test: test_model_scaling.c (update existing)

- Verify `scaler_model_file()` returns correct filenames for each state
- Verify scaler transitions with simulated miss rates: 0.1 → IDLE, 0.6 → ACTIVE, 0.9 → CRITICAL
- Verify hysteresis: brief miss rate spike doesn't cause thrashing

### QEMU NVMe Integration Test

1. Create virtual NVMe image with all 3 GGUF files
2. Boot in QEMU, verify IDLE model loads (Llama 1B)
3. Manually trigger high miss rate (send queries that miss cache)
4. Observe swap to ACTIVE (Gemma 4 E2B) in NVMe log
5. Verify E2B generates coherent text after swap
6. Reduce miss rate, verify de-escalation back to IDLE

### SWAP_FAILED Double-Failure Test

Verify the system degrades gracefully when BOTH the new model AND the fallback fail:
1. Trigger escalation (IDLE → ACTIVE)
2. Corrupt or remove `GEMMA2B.GUF` on NVMe → PB sends SWAP_FAILED
3. PA tries to reload previous model (`LLAMA1B.GUF`) — also remove it → SWAP_FAILED
4. PA must force EMERGENCY, log diagnostic, and continue serving cache hits
5. Verify cache-only mode stays responsive (cache hits still return <1ms)
6. Verify NVMe log captures the full failure sequence

### Bare Metal Test

Same as QEMU but on JARVIS PC with real NVMe. Requires:
- Resize ESP or add model partition (16GB)
- Copy all 3 GGUF files with proper 8.3 names
- Boot, trigger scaling transitions, verify via NVMe log

## 13. Known Limitations

- **Miss rate over-escalation:** Cache misses don't distinguish "novel but easy query" from "genuinely complex query needing a smarter model." A burst of novel-but-simple queries (e.g., new topic with no cache patterns) triggers unnecessary escalation. The hysteresis (10 evaluations below idle threshold) limits thrashing but doesn't prevent the initial over-escalation.
  <!-- TODO v2: Add complexity hinting — classify queries by token count, embedding distance from cache patterns, or PB's generation entropy. Feed complexity score into scaler alongside miss rate. -->

- **30-60s cache-only window is acceptable for Phase 3 stability testing** but NOT for production. Users would experience degraded responses during swap. Phase 4 should implement zero-downtime swap (see Future Work).

- **Single-file-at-a-time NVMe reads:** Model loading is sequential. The 7.2GB Mistral 7B takes ~150s to load. No parallelism with inference during load.

## 14. Future Work (v2)

- **Double-memory swap for zero downtime:** Pre-load new model into a second set of pages while PB continues inference on the current model. Atomic swap when new model is ready. Requires 2x model memory during transition (~8GB + 3GB = 11GB for IDLE→ACTIVE, feasible with 32GB RAM). Eliminates the cache-only window entirely.

- **Complexity-aware scaling signal:** Replace raw cache miss rate with a composite signal: miss rate + average query token count + cache embedding distance. This distinguishes "novel but simple" from "genuinely needs a bigger model" and reduces unnecessary escalation.

- **seL4 model scaling:** The current design runs on native Linux (Process B has full NVMe access via shared pages mapped by PA). On seL4, PB's morecore limit may prevent reallocation for larger models. Phase 4 needs to pre-allocate state buffers for the largest tier (Mistral 7B) at boot, then use subsets for smaller models.

## 15. Non-Goals

- **Prompt batching:** Single-query design stays. No queue management.
- **Concurrent model loading:** Only one model in memory at a time (see Future Work for v2).
- **Quality-based scaling:** Scaler uses miss rate, not response quality.
- **seL4 threading for swap:** NVMe reads are single-threaded in PA.
- **Automatic model download:** Models must be pre-staged on NVMe.
