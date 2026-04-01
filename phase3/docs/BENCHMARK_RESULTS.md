# JARVIS AI-OS — LLM Benchmark Results

**Purpose:** Baseline CPU-only inference benchmarks vs TurboQuant KV cache compression.
**Hardware:** AMD Ryzen 7 2700X (8C/16T) | 48GB DDR4 | No GPU
**Software:** llama.cpp (CPU-only build) | JARVIS TurboQuant (phase3/src/ai/)
**Date Started:** April 1, 2026

---

## Hardware Profile

| Component | Spec |
|-----------|------|
| CPU | AMD Ryzen 7 2700X (8 cores / 16 threads, 3.7 GHz base) |
| RAM | 48 GB DDR4 |
| GPU | None (removed from system) |
| OS | Linux (Ubuntu) |
| Quantization | Q4_K_M (all models) |

---

## Test Protocol

- **3 prompts per model**, increasing output length (64 / 128 / 256 tokens)
- **Prompt 1 (Factual):** "What is the seL4 microkernel and why is it important for security?"
- **Prompt 2 (Code):** "Write a lock-free SPSC ring buffer in C with push and pop functions."
- **Prompt 3 (Analysis):** "Compare KV cache compression: quantization vs eviction vs sparse attention. Which is best for edge devices with 1B-3B models?"
- **Metrics captured:** prompt eval tok/s, generation tok/s, VRAM/RAM breakdown, wall time
- **Context size:** 4096 tokens (8B/13B), 2048 tokens (70B)
- **Threads:** 8 (`-t 8`)
- **Temperature:** 0.7, top-k default

---

## Section 1: Baseline (llama.cpp CPU-only, no TurboQuant)

### Previous Baseline (RTX 2070 + 32GB RAM — March 30, 2026)

| Model | ngl | Gen tok/s | Prompt tok/s | VRAM Used | Host RAM | Notes |
|-------|-----|-----------|-------------|-----------|----------|-------|
| 8B Q4_K_M | 99 (full GPU) | 62 t/s | 641-855 t/s | 5,173 MiB | 305 MiB | Excellent |
| 13B Q4_K_M | 20 (split) | 7.7 t/s | 85-112 t/s | 5,246 MiB | 5,618 MiB | GPU-CPU bus bottleneck |
| 70B Q4_K_M | 10 (mostly CPU) | 0.1 t/s | 1.0 t/s | 5,903 MiB | 35,655 MiB | Swap-thrashed (32GB insufficient) |

### Current Baseline (CPU-only, 48GB RAM — April 1, 2026)

#### Llama 3.1 8B Instruct Q4_K_M (4.6 GB)

| Prompt | Tokens | Prompt eval | Generation | RAM Used | RAM Avail |
|--------|--------|-------------|------------|----------|-----------|
| 1 (Factual) | 64 | 13.3 t/s | 6.4 t/s | 4,374 MB | 43,701 MB |
| 2 (Code) | 128 | 13.1 t/s | 6.3 t/s | 4,339 MB | 43,736 MB |
| 3 (Analysis) | 256 | 12.7 t/s | 6.1 t/s | 4,339 MB | 43,735 MB |

**Memory Breakdown:**
- Model: 4,685 MiB
- Context (KV cache): 512 MiB
- Compute: 266 MiB
- CPU_REPACK: 3,204 MiB
- **Total host: 8,667 MiB (~8.5 GB)**

#### Llama 2 13B Q4_K_M (7.4 GB)

| Prompt | Tokens | Prompt eval | Generation | RAM Used | RAM Avail |
|--------|--------|-------------|------------|----------|-----------|
| 1 (Factual) | 64 | 6.8 t/s | 3.9 t/s | 4,357 MB | 43,718 MB |
| 2 (Code) | 128 | 7.0 t/s | 3.8 t/s | 4,364 MB | 43,711 MB |
| 3 (Analysis) | 256 | 7.2 t/s | 3.7 t/s | 4,357 MB | 43,718 MB |

**Memory Breakdown:**
- Model: 7,500 MiB
- Context (KV cache): 3,200 MiB
- Compute: 133 MiB
- CPU_REPACK: 5,765 MiB
- **Total host: 16,598 MiB (~16.2 GB)**

#### Llama 3.1 70B Instruct Q4_K_M (40 GB)

| Prompt | Tokens | Prompt eval | Generation | RAM Used | RAM Avail |
|--------|--------|-------------|------------|----------|-----------|
| 1 (Factual) | 64 | 1.2 t/s | 0.7 t/s | ~45,600 MB | ~2,475 MB |
| 2 (Code) | 128 | 1.3 t/s | 0.7 t/s | ~45,600 MB | ~2,475 MB |
| 3 (Analysis) | 256 | 1.3 t/s | 0.7 t/s | ~45,600 MB | ~2,475 MB |

**Memory Breakdown:**
- Model: 40,543 MiB
- Context (KV cache): 640 MiB
- Compute: 282 MiB
- CPU_REPACK: 31,320 MiB
- **Total reported: 72,785 MiB (virtual — includes mmap + CPU_REPACK)**
- **Actual RAM resident: ~45,600 MiB (~44.5 GB) — fits in 48GB without swap**

---

## Section 2: TurboQuant KV Cache Compression

**Method:** Inject TQ compress-decompress roundtrip into llama.cpp KV cache write path via `ggml_map_custom1`. Lossy (TQ-reconstructed) values stored in the standard F16 cache. Everything else identical to baseline.

**Implementation:** llama.cpp `feature/turboquant` branch. Patch: `phase3/benchmarks/turboquant-llamacpp.patch`.

### TurboQuant Configuration

| Parameter | 3.5-bit | 2.5-bit |
|-----------|---------|---------|
| Key bits (outlier / regular) | 4 / 3 | 3 / 2 |
| Value bits (outlier / regular) | 4 / 3 | 3 / 2 |
| Outlier coords | 32 of 128 (25%) | 32 of 128 (25%) |
| Head dimension | 128 (all three models) | 128 |
| Codebook | Beta-optimal (arXiv 2504.19874) | Beta-optimal |
| QJL correction | Yes (keys only, 1-bit signs) | Yes |

### Results: TurboQuant 3.5-bit

#### 8B (3.5-bit)

| Prompt | Tokens | Prompt eval | Generation | Quality |
|--------|--------|-------------|------------|---------|
| 1 (Factual) | 64 | 12.5 t/s | 5.7 t/s | Coherent |
| 2 (Code) | 128 | 20.4 t/s | 5.8 t/s | Coherent |
| 3 (Analysis) | 256 | 13.4 t/s | 5.4 t/s | Coherent |

**Memory:** Model 4,685 MiB + Context 512 MiB + CPU_REPACK 3,204 MiB (identical to baseline — TQ is roundtrip simulation)

#### 13B (3.5-bit)

| Prompt | Tokens | Prompt eval | Generation | Quality |
|--------|--------|-------------|------------|---------|
| 1 (Factual) | 64 | 6.5 t/s | 2.5 t/s | Coherent |
| 2 (Code) | 128 | 6.6 t/s | 2.4 t/s | Coherent |
| 3 (Analysis) | 256 | 6.5 t/s | 2.5 t/s | Coherent |

**Memory:** Model 7,500 MiB + Context 3,200 MiB + CPU_REPACK 5,765 MiB

#### 70B (3.5-bit)

| Prompt | Tokens | Prompt eval | Generation | Quality |
|--------|--------|-------------|------------|---------|
| 1 (Factual) | 64 | 1.2 t/s | 0.8 t/s | Coherent |
| 2 (Code) | 128 | 1.2 t/s | 0.7 t/s | Coherent |
| 3 (Analysis) | 256 | 1.3 t/s | 0.7 t/s | Coherent |

**Memory:** Model 40,543 MiB + Context 640 MiB + CPU_REPACK 31,320 MiB

### Results: TurboQuant 2.5-bit

#### 8B (2.5-bit) — QUALITY FAILURE

| Prompt | Tokens | Prompt eval | Generation | Quality |
|--------|--------|-------------|------------|---------|
| 1 (Factual) | 64 | 13.1 t/s | 5.8 t/s | **DEGENERATE** — "And so the and so why and so so..." |
| 2 (Code) | 128 | 13.0 t/s | 5.7 t/s | **DEGENERATE** — "Push-pushed-pushed-push-push..." |
| 3 (Analysis) | 256 | 12.5 t/s | 5.3 t/s | **DEGENERATE** — "a a a a a a a a a a a a..." |

#### 13B (2.5-bit) — QUALITY FAILURE

| Prompt | Tokens | Prompt eval | Generation | Quality |
|--------|--------|-------------|------------|---------|
| 1 (Factual) | 64 | 6.4 t/s | 2.6 t/s | **DEGENERATE** — repeated tokens |
| 2 (Code) | 128 | 6.3 t/s | 2.6 t/s | **DEGENERATE** |
| 3 (Analysis) | 256 | 6.6 t/s | 2.6 t/s | **DEGENERATE** |

#### 70B (2.5-bit) — MIXED

| Prompt | Tokens | Prompt eval | Generation | Quality |
|--------|--------|-------------|------------|---------|
| 1 (Factual) | 64 | 1.2 t/s | 0.7 t/s | Coherent (70B more robust) |
| 2 (Code) | 128 | 1.2 t/s | 0.7 t/s | Coherent |
| 3 (Analysis) | 256 | 1.3 t/s | 0.8 t/s | Coherent |

---

## Section 3: Comparison Summary

### 3.5-bit TurboQuant vs Baseline

| Model | Metric | Baseline (CPU) | TQ 3.5-bit | Delta |
|-------|--------|----------------|------------|-------|
| 8B | Gen tok/s | 6.1-6.4 | 5.4-5.8 | -10% to -12% (TQ overhead) |
| 8B | Quality | Coherent | Coherent | No degradation |
| 13B | Gen tok/s | 3.7-3.9 | 2.4-2.5 | -33% to -36% (TQ overhead) |
| 13B | Quality | Coherent | Coherent | No degradation |
| 70B | Gen tok/s | 0.7 | 0.7-0.8 | ~0% (TQ overhead hidden by memory bandwidth) |
| 70B | Quality | Coherent | Coherent | No degradation |

### 2.5-bit TurboQuant vs Baseline

| Model | Metric | Baseline (CPU) | TQ 2.5-bit | Delta |
|-------|--------|----------------|------------|-------|
| 8B | Gen tok/s | 6.1-6.4 | 5.3-5.8 | ~-10% |
| 8B | Quality | Coherent | **DEGENERATE** | **FAIL** |
| 13B | Gen tok/s | 3.7-3.9 | 2.6 | ~-33% |
| 13B | Quality | Coherent | **DEGENERATE** | **FAIL** |
| 70B | Gen tok/s | 0.7 | 0.7-0.8 | ~0% |
| 70B | Quality | Coherent | Coherent | OK (70B robust enough) |

### Analytical KV Cache Compression (what TQ would save if stored compressed)

| Model | F16 KV cache | TQ 3.5-bit KV (estimated) | Compression |
|-------|-------------|---------------------------|-------------|
| 8B | 512 MiB | ~110 MiB | ~4.7x |
| 13B | 3,200 MiB | ~690 MiB | ~4.6x |
| 70B | 640 MiB | ~140 MiB | ~4.6x |

### Key Findings

1. **3.5-bit TurboQuant preserves output quality** across all three models (8B, 13B, 70B). No visible degradation in coherence or factual accuracy. This matches the paper's result of 50.06 LongBench (identical to full precision).

2. **2.5-bit TurboQuant causes catastrophic quality failure on 8B and 13B.** Output degenerates into repetitive tokens. Only the 70B model is robust enough to tolerate 2.5-bit compression. This is more aggressive than the paper's reported 49.44 LongBench score suggests — short-context generation is more sensitive than long-context benchmarks.

3. **Speed overhead is 10-36%.** The compress-decompress roundtrip adds computational overhead (matrix-vector multiply for rotation + scalar quantization per coordinate). This overhead is larger on 13B (~33%) than 8B (~10%) or 70B (~0%). On 70B, memory bandwidth dominates and the TQ overhead is negligible.

4. **Memory savings are analytical only in this benchmark.** The roundtrip approach stores lossy F32→F16 in the standard cache. Actual memory savings (4.6x compression) would require a custom compressed cache format, which is Phase 2 of this work.

5. **d=128 works; d=64 doesn't.** Previous tests on Llama 3.2 1B (d=64) showed 8.9% token match. The d=128 models (8B, 13B, 70B) work well at 3.5-bit. The paper only tests d=128+ models, confirming this is the intended operating regime.

---

## Notes

- All tests run with `llama-cli -cnv --single-turn --simple-io --no-display-prompt`
- Memory breakdown from llama.cpp `llama_memory_breakdown_print` output
- "Quality" measured by coherence of output (subjective) and token match vs baseline
