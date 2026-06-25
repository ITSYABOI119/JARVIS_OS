# Third-Party Licenses

JARVIS AI-OS is licensed under the MIT License (see [`LICENSE`](LICENSE)). It bundles and depends on
the following third-party components, each under its own license.

## Bundled in this repository (`phase4/console/vendor/`)

These pinned, offline browser-runtime libraries are committed so the Remote Telemetry Console serves
hermetically (no live CDN). Each vendored file retains its own upstream license banner; full license
texts are available from the upstream projects.

| Component | Version | License | Copyright |
|-----------|---------|---------|-----------|
| React (`react.development.js`) | 18.3.1 | MIT | © Meta Platforms, Inc. and affiliates |
| ReactDOM (`react-dom.development.js`) | 18.3.1 | MIT | © Meta Platforms, Inc. and affiliates |
| @babel/standalone (`babel.min.js`) | 7.29.0 | MIT | © Sebastian McKenzie and the Babel contributors |
| Lucide (`lucide.min.js`) | 1.21.0 | ISC | © Lucide Contributors |

## Build & runtime dependencies (NOT distributed in this repository)

- **seL4 microkernel** — kernel under **GPLv2**, userspace libraries (libsel4, etc.) under
  **BSD-2-Clause** — © seL4 Project / Proofcraft / Data61. Fetched and built out-of-tree by
  `phase3/scripts/build_jarvis_x86.sh`; no seL4 source is included here. JARVIS's running
  configuration (`KernelFastpath=ON` + SMP) is outside seL4's verified X64 config —
  functional-but-unverified by design (see `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` §3).
- **Gemma model (Gemma 4 E2B)** — Google **Gemma Terms of Use** (NOT an OSI-approved open-source
  license) — © Google. Provided by the operator at runtime (loaded from the NVMe `JARVIS_DATA`
  partition); the model weights are NOT included in this repository.

## Inference engine — format compatibility (no third-party source distributed)

JARVIS's inference engine (`phase3/src/ai/`) is an independent, from-scratch implementation. It reads
the **GGUF** file format and its quantization block layouts, and uses the `ggml_type_t` type tags, to
stay compatible with — and is validated bit-exact against — the **ggml / llama.cpp** project
(MIT, © Georgi Gerganov and contributors). No ggml or llama.cpp source code is distributed in this
repository; the optional `ggml_backend_stubs.c` contains JARVIS-authored stubs only (used to link the
real ggml objects in a non-distributed, non-CI integration test).
