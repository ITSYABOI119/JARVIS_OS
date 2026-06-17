# JARVIS Daily Tech Briefing — Wednesday, June 17, 2026

**Lead — EAGLE3 speculative decoding lands in llama.cpp (build b9669, Jun 16).** The EAGLE3 draft-head support (PR #18039) is now in tree, and b9669 adds backend draft-sampling on top of it. *Why it matters:* speculative decoding is a direct lever on token-generation throughput — exactly your Phase 4 goal #1 (closing the ~2x tok/s gap to llama.cpp) — and a draft-head approach could lift the seL4-build rate without new SIMD work. [GitHub releases](https://github.com/ggml-org/llama.cpp/releases/) · [PR #18039](https://github.com/ggml-org/llama.cpp/pull/18039)

## Local LLM Inference

- **EAGLE3 in llama.cpp — how it works (Jun 16):** target model records low/mid/high hidden states, a lightweight speculative layer autoregressively proposes draft tokens, and the target verifies them in parallel; b9669 offloads draft sampling to the backend and supports tree attention + batch>1. Trade-off for JARVIS: needs a draft model + extra KV memory, so it's a Phase-4 throughput experiment, not a free win — but the accept-rate gains are larger than plain n-gram drafting. [PR #18039](https://github.com/ggml-org/llama.cpp/pull/18039) · [releases](https://github.com/ggml-org/llama.cpp/releases/)

## Quantization

- **Community llama.cpp fork combines 1-bit + RotorQuant + TurboQuant + EAGLE3 (`llama.cpp-1-bit-turbo`):** a third-party HIP/ROCm fork (AMD RDNA2-targeted) shipping a `Q1_0_G128` 1-bit quant plus RotorQuant and TurboQuant KV compression and P-EAGLE. Niche and GPU-oriented, but it's a concrete reference impl of the exact KV-quant methods you evaluated and deferred to Phase 4 — useful to read before you revisit TQ/RQ. [GitHub](https://github.com/carlosfundora/llama.cpp-1-bit-turbo)

## Systems / OS

- **Haiku OS enables AVX-512 (Phoronix, Jun 13):** waddlesplash/trungnt2910 reworked the kernel's FPU handling so AVX-512 state is preserved across context switches on capable CPUs. *Why it matters:* this is the same class of change as your M0 — rebuilding the seL4 kernel to XSAVE/feature-set 7 so it context-switches AVX YMM. A fresh, independent reference for getting the FPU-state save/restore path right. [Phoronix](https://www.phoronix.com/news/Haiku-OS-May-2026)
- **Linux sanitizes RISC-V syscall-table indexing under speculation (6.19-rc5, ~Jun 13):** Spectre-style guard on the user-controlled syscall number before it indexes the syscall table, matching existing x86/ARM handling. Evergreen reminder for anyone hand-rolling syscall dispatch on bare metal. [Phoronix](https://www.phoronix.com/news/Linux-6.19-RISC-V-Side-Channel)
- **Update — Arch AUR malware now "under control," 1,500+ packages (Jun 15):** the supply-chain incident from prior briefings has grown to 1,500+ affected packages; Arch says it's contained and is rolling back. If your dev/build host touches AUR or runs `npm install` in builds, still worth an audit. [Phoronix](https://www.phoronix.com/)

---
*De-duplicated against the Jun 16 and earlier Jun 17 briefings (Retout seL4 clock post, llama.cpp b9670 generic build, Ollama/Gemma 4 QAT, MiniMax/Nemotron, Linux 7.1 NTFS, Kell syscall-alignment, seL4 Summit/Riverside all dropped as already-covered). seL4/Microkernels and Model Releases omitted — nothing net-new in the window. "Recent" = roughly Jun 13–17, 2026; sandbox 