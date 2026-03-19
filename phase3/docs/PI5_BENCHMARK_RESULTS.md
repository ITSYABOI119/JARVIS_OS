# JARVIS AI-OS: Pi 5 LLM Benchmark Results

**Date:** March 19, 2026
**Hardware:** Raspberry Pi 5, 4GB LPDDR4X, Cortex-A76 4C @ 2.4 GHz
**Purpose:** Determine practical LLM inference limits on Pi 5 4GB for Phase 3 planning
**Script:** `phase3/scripts/pi5_benchmark.sh`

---

## 1. Research-Based Estimates

The following numbers are compiled from published benchmarks by multiple independent
sources. All use **Ollama** on Raspberry Pi 5 (8GB, unless noted). The 4GB model will
perform identically for models that fit in RAM, but will degrade heavily for models
that exceed ~3GB resident (due to swap thrashing).

### 1.1 Ollama Benchmark Data (Estimated from Web Benchmarks)

| Model | Params | Quant | Eval tok/s (est.) | Prompt tok/s (est.) | RAM Usage | Disk Size | Source |
|-------|--------|-------|-------------------|---------------------|-----------|-----------|--------|
| gemma3:1b | 1B | Q4_K_M | **~13** | ~20-30 | ~2.5 GB | ~0.8 GB | Stratosphere Labs |
| llama3.2:1b-instruct | 1B | Q4_K_M | **~12** | ~18-25 | ~2.8 GB | ~0.7 GB | Stratosphere Labs |
| qwen2.5:1.5b | 1.5B | Q4_K_M | **~11** | ~15-22 | ~2.2 GB | ~1.0 GB | Stratosphere Labs |
| granite3.1-dense:2b | 2B | Q4_K_M | **~9** | ~12-18 | ~3.5 GB | ~1.3 GB | Stratosphere Labs |
| smollm2:1.7b-instruct | 1.7B | Q8_0 | **~7** | ~10-15 | ~5.0 GB | ~1.8 GB | Stratosphere Labs |
| llama3.2:3b | 3B | Q4_K_M | **~5** | ~8-12 | ~4.2 GB | ~2.0 GB | Stratosphere Labs, aidatatools |
| qwen2.5:3b | 3B | Q4_K_M | **~5** | ~8-12 | ~3.8 GB | ~1.9 GB | Stratosphere Labs |

### 1.2 llama.cpp Direct (Estimated from Web Benchmarks)

llama.cpp is 10-20% faster than Ollama due to less overhead. Llamafile can be up to
4x faster than Ollama on ARM SBCs according to the arxiv 2511.07425 evaluation.

| Model | Params | Quant | Eval tok/s (est.) | Notes |
|-------|--------|-------|-------------------|-------|
| gemma3:1b | 1B | Q4_K_M | **~15-16** | +15% over Ollama |
| llama3.2:1b | 1B | Q4_K_M | **~14-15** | +15% over Ollama |
| qwen2.5:1.5b | 1.5B | Q4_K_M | **~13** | +15% over Ollama |
| llama3.2:3b | 3B | Q4_K_M | **~6-7** | +20% over Ollama, needs swap on 4GB |
| llama3.2:3b | 3B | Q3_K_S | **~5-6** | Smaller quant to reduce RAM |

### 1.3 Overclocking Estimates

Pi 5 can be overclocked from 2.4 GHz to 2.8 GHz with active cooling, yielding
~10-15% performance improvement on CPU-bound LLM workloads.

| Model | Stock (2.4 GHz) | OC (2.8 GHz, est.) | Notes |
|-------|-----------------|---------------------|-------|
| gemma3:1b (Ollama) | ~13 tok/s | ~15 tok/s | Requires active cooling |
| llama3.2:1b (Ollama) | ~12 tok/s | ~14 tok/s | Requires active cooling |
| llama3.2:3b (Ollama) | ~5 tok/s | ~5.5-6 tok/s | Marginal gain, still swap-bound |

---

## 2. Actual Measured Results (Pi 5 Hardware)

**Status:** NOT YET MEASURED - Run `phase3/scripts/pi5_benchmark.sh` on Pi 5 hardware.

### 2.1 Ollama Benchmarks (Actual)

| Model | Params | Quant | Eval tok/s (actual) | Prompt tok/s (actual) | RAM Usage (actual) | CPU Temp | Notes |
|-------|--------|-------|--------------------|-----------------------|--------------------|----------|-------|
| gemma3:1b | 1B | Q4_K_M | ___ | ___ | ___ MB | ___C | |
| llama3.2:1b-instruct | 1B | Q4_K_M | ___ | ___ | ___ MB | ___C | |
| qwen2.5:1.5b | 1.5B | Q4_K_M | ___ | ___ | ___ MB | ___C | |
| granite3.1-dense:2b | 2B | Q4_K_M | ___ | ___ | ___ MB | ___C | |
| llama3.2:3b | 3B | Q4_K_M | ___ | ___ | ___ MB | ___C | OOM? |
| qwen2.5:3b | 3B | Q4_K_M | ___ | ___ | ___ MB | ___C | OOM? |

### 2.2 System Observations (Actual)

- Available RAM before benchmark: ___ MB (of 4096 MB total)
- Swap used during 1B models: ___ MB
- Swap used during 3B models: ___ MB
- Did 3B models run successfully? ___
- Thermal throttling observed? ___
- Active cooling installed? ___

---

## 3. Memory Analysis: Can a 3B Q4 Model Fit in 4GB?

### 3.1 Memory Budget (Pi 5, 4GB)

```
Total physical RAM:              4,096 MB
- Linux kernel + services:        ~500 MB  (minimal headless Pi OS)
- Ollama server process:          ~100 MB
- Available for model + context:  ~3,400 MB (theoretical max)
                                  ~3,000 MB (practical, with overhead)
```

### 3.2 Model Memory Requirements

| Model | Weights (Q4_K_M) | KV Cache (ctx=512) | KV Cache (ctx=2048) | Total (ctx=512) | Total (ctx=2048) |
|-------|-------------------|--------------------|---------------------|-----------------|------------------|
| 1B | ~0.6 GB | ~0.1 GB | ~0.4 GB | **~0.7 GB** | **~1.0 GB** |
| 1.5B | ~0.9 GB | ~0.1 GB | ~0.5 GB | **~1.0 GB** | **~1.4 GB** |
| 3B (Q4_K_M) | ~1.8 GB | ~0.2 GB | ~0.8 GB | **~2.0 GB** | **~2.6 GB** |
| 3B (Q3_K_S) | ~1.4 GB | ~0.2 GB | ~0.8 GB | **~1.6 GB** | **~2.2 GB** |

### 3.3 Verdict: 3B on 4GB

**3B Q4_K_M with context 512: TIGHT but POSSIBLE.**

- Weights (1.8 GB) + KV cache (0.2 GB) + runtime = ~2.5 GB
- OS + Ollama overhead = ~0.6 GB
- Total: ~3.1 GB of 4.0 GB physical
- Leaves ~900 MB headroom -- barely enough, no room for context expansion

**3B Q4_K_M with context 2048: WILL SWAP.**

- Total: ~3.2 GB model + ~0.6 GB OS = ~3.8 GB
- Exceeds comfortable limit, will use swap
- Performance degrades to ~1-2 tok/s when swapping
- MicroSD swap is extremely slow; USB SSD swap helps somewhat

**Practical recommendation for 4GB Pi 5:**

- **Use 1B-1.5B models** (gemma3:1b, qwen2.5:1.5b) for reliable operation
- **Context size 512-1024** tokens max for 1.5B models
- **3B models are not practical** on 4GB without external SSD swap
- If 3B is needed, use Q3_K_S quantization with `--ctx-size 512`

---

## 4. Comparison: Pi 5 vs RTX 3060 GPU (Host PC)

Data from `PHASE_3_HARDWARE_RESEARCH.md` for the spare x86 PC (Ryzen 5 5600 + RTX 3060 12GB).

| Metric | Pi 5 4GB (Ollama) | RTX 3060 12GB (CUDA) | Ratio |
|--------|-------------------|----------------------|-------|
| **gemma3:1b / Llama 1B** | ~12-13 tok/s | ~100+ tok/s (est.) | **~8x slower** |
| **Phi-3 Mini 3.8B** | Cannot run (OOM) | ~90-100 tok/s (est.) | **N/A** |
| **Llama 3.2 3B** | ~5 tok/s (with swap) | ~100+ tok/s (est.) | **~20x slower** |
| **Llama 2 7B** | Cannot run (OOM) | 75-77 tok/s | **N/A** |
| **Llama 3.1 8B** | Cannot run (OOM) | ~57 tok/s | **N/A** |
| **13B models** | Cannot run (OOM) | ~9-18 tok/s | **N/A** |
| Max model size | 1.5B comfortable | 13B comfortable | **~9x capacity** |
| Max context window | 512-1024 tokens | 4096-8192 tokens | **~8x context** |
| Power consumption | ~8-10W | ~200W (GPU under load) | **20x more efficient** |
| Cost | $0 (owned) | $0 (owned) | Equal |
| IPC to seL4 Pi 4 | 7ms (UART) | 7ms (UART) | Equal (Phase 3a) |

### Key Takeaways

1. **The RTX 3060 is 8-20x faster** for inference and can run models 9x larger.
2. **Pi 5 cannot run anything above 1.5B** reliably on 4GB RAM.
3. **Pi 5 is 20x more power-efficient** (8W vs 200W), which matters for 24/7 operation.
4. **Both connect to Pi 4 via UART at 7ms** in Phase 3a, so IPC latency is identical.
5. **Pi 5 is viable as a lightweight fallback** or secondary node, not as primary AI host.

---

## 5. Phase 3 Planning Implications

### 5.1 Option B (Multi-Pi Cluster) Assessment

The Pi 5 4GB as AI inference node is **viable but severely constrained:**

| Use Case | Feasible? | Notes |
|----------|-----------|-------|
| Basic JARVIS queries (1B model) | YES | ~12-13 tok/s, responsive enough |
| Complex reasoning (3B+ model) | NO | OOM or swap-degraded |
| Multi-turn conversations | LIMITED | Context window capped at 512-1024 tokens |
| Sustained 24/7 operation | YES | Low power, active cooling needed |
| Model upgrades in future | NO | 4GB is a hard ceiling |

**Verdict:** Pi 5 4GB works for simple queries with 1B models. The decision cache
(85% hit rate, <1ms) means only 15% of queries reach the LLM, so even 12-13 tok/s
is acceptable for cache misses. However, the 4GB RAM cap means this path has no
growth potential.

### 5.2 Recommendation for Phase 3

**Primary path: Option D (Hybrid x86, per PHASE_3_HARDWARE_RESEARCH.md)**

The RTX 3060 is simply in a different league: 7B models at 75 tok/s vs 1B models
at 13 tok/s. For a project building toward a standalone AI operating system, the
x86 path provides the capability headroom that the Pi 5 cannot.

**Secondary path: Pi 5 as quick experiment (1-2 weeks)**

Running the benchmark script on the actual Pi 5 hardware is still worthwhile:
1. Validates real-world numbers (estimated vs measured)
2. Tests the JARVIS decision cache + LLM pipeline end-to-end on cheap hardware
3. Provides a low-power fallback node if the spare PC is unavailable
4. Data useful for future ARM-based deployment research

### 5.3 If Pi 5 8GB Were Available

For reference, a Pi 5 with 8GB RAM would change the picture:
- 3B Q4_K_M models would run at ~5 tok/s without swapping
- Context windows of 2048 tokens would be practical
- Still 15x slower than RTX 3060, but usable for real work
- The 8GB model costs ~$80 and would be a meaningful upgrade

---

## 6. Data Sources

All estimated numbers in Section 1 are from published benchmarks, NOT from our
hardware. They will be validated against actual measurements from
`pi5_benchmark.sh` when run on the Pi 5.

| Source | URL | Data Used |
|--------|-----|-----------|
| Stratosphere Labs (Pi 5 LLM benchmarks) | https://www.stratosphereips.org/blog/2025/6/5/how-well-do-llms-perform-on-a-raspberry-pi-5 | tok/s, RAM for gemma3, llama3.2, qwen2.5, granite3.1 |
| arxiv 2511.07425 (SBC LLM Evaluation) | https://arxiv.org/html/2511.07425v1 | Ollama vs Llamafile, 1B-3B performance bands |
| aidatatools (Pi 5 Ollama Llama3.2 bench) | https://medium.com/aidatatools/raspberry-pi-os-2024-10-22-benchmark-for-ollama-llama3-2-3b-and-1b-c649ebc1acd4 | llama3.2 1b/3b across OS versions |
| It's FOSS (9 LLMs on Pi 5) | https://itsfoss.com/llms-for-raspberry-pi/ | Inference time, RAM for 9 models |
| Raspberry Pi Forums (overclocking) | https://forums.raspberrypi.com/viewtopic.php?t=384602 | OC performance gains |
| Ollama vs llama.cpp on Pi 5 | https://medium.com/@omkar121212/ollama-vs-llama-cpp-on-raspberry-pi-5-8e7fbeb310de | llama.cpp 10-20% faster than Ollama |
| llama.cpp vs llamafile on Pi 5 | https://medium.com/aidatatools/local-llm-eval-tokens-sec-comparison-between-llama-cpp-and-llamafile-on-raspberry-pi-5-8gb-model-89cfa17f6f18 | Llamafile up to 4x faster |
| AI Competence (Pi 5 Llama guide) | https://aicompetence.org/running-llama-on-raspberry-pi-5/ | Setup, RAM constraints |
| llama.cpp CUDA discussion | https://github.com/ggml-org/llama.cpp/discussions/15013 | RTX 3060 7B benchmark |
| RTX 3060 Ti Ollama benchmarks | https://www.databasemart.com/blog/ollama-gpu-benchmark-rtx3060ti | RTX 3060 Ti model performance |

---

*Research compiled: March 19, 2026*
*Actual benchmarks: PENDING (run pi5_benchmark.sh on Pi 5 hardware)*
*Next step: Connect Pi 5 to monitor + network, run benchmark, fill in Section 2*
