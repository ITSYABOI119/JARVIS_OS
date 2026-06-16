# JARVIS Daily Tech Briefing — Tuesday June 16, 2026

**Lead — Active "Atomic Arch" AUR supply-chain attack.** 400+ Arch User Repository packages (some trackers say 1,600+) were backdoored by attackers who adopted orphaned packages and edited PKGBUILDs to pull a malicious npm dependency (`atomic-lockfile` / `js-digest` / `lockfile-js`), dropping a Rust infostealer plus an eBPF rootkit that steals SSH keys/tokens and hides itself. Arch has locked AUR signups and is rolling back. *Why it matters:* if your dev/toolchain host pulls AUR or runs `npm install` in builds, audit now — an eBPF-concealed host compromise is exactly what you don't want on the machine that builds a bare-metal seL4 target. [The Hacker News](https://thehackernews.com/2026/06/over-400-arch-linux-aur-packages.html) · [Phoronix](https://www.phoronix.com/news/Arch-Linux-AUR-400-Compromised)

## seL4 / Microkernels

- **seL4 clock-magic + merged upstream patch (Jun 12).** Tim Retout walks through seL4's clock handling and landed a small patch removing a legacy Python module from a build helper script — minor, but a clean example of running seL4 tooling on a stripped-down host. [retout.co.uk](https://retout.co.uk/2026/06/12/sel4-clock-magic/) · [commit](https://github.com/seL4/seL4/commit/c1c9dd5bff69f5ea078d341a47555ad485808ab4)
- **MCS verification on track for RISC-V in 2026 (ongoing).** Microkit's MCS (mixed-criticality scheduling) seL4 config is slated to finish functional-correctness verification on RISC-V this year (AArch64 in 2027). Worth tracking — JARVIS leans on seL4 priority/`seL4_Yield` semantics that MCS reworks. [seL4 docs](https://docs.sel4.systems/projects/microkit/manual/latest/)

## Local LLM Inference

- **llama.cpp shipping daily builds (latest b9644, Jun 15).** Recent merges add a Cohere2-MoE ("North Code") architecture + chat parser and TINY_AYA vocab, plus jinja chat-template fixes and build-time UI gzip. *Why it matters:* llama.cpp is JARVIS's speed/coverage reference (the ~2x gap target); new model-family support is the same coverage work the JARVIS engine bench tracks. [Releases](https://github.com/ggml-org/llama.cpp/releases)

## Systems / OS

- **Linux 7.1 released (Jun 14).** Headline is a brand-new in-tree NTFS implementation with full write support — 4 years of work, with delayed allocation, iomap and folio integration, plus a new `ntfsprogs-plus` userspace suite. [9to5Linux](https://9to5linux.com/linux-kernel-7-1-officially-released-heres-whats-new)
- **"What stack alignment does an x86-64 Linux syscall require?" (Jun 12).** Stephen Kell finds the kernel's syscall ABI alignment requirement is effectively undocumented — a sharp low-level read for anyone hand-rolling syscall/ABI code on bare-metal x86-64. [humprog.org](https://www.humprog.org/~stephen/blog/2026/06/12/#system-calling-alignment)

## Model Releases / Quantization

Quiet — no new small-model drops or quant formats in the last 1–2 days. Only ripple is llama.cpp landing Cohere2-MoE tooling (above). Latest relevant families remain Gemma 4 (Apr), Qwen 3.5 (Mar), Phi-4 variants.
