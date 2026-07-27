# Model options — what a bigger model would actually cost

**Date:** 2026-07-27 · **Status:** RESEARCH, no decision taken
**Context:** raised after `THINKING_MODE_RESEARCH.md` concluded that the reasoning headline
(*AIME 2026 89.2%*) belongs to a Gemma 4 family member we do not run — E2B scores 37.5% on the same
benchmark. "Should JARVIS think?" turned out to be "should JARVIS run a bigger model?"

---

## 1. The binding constraint is BANDWIDTH, not RAM

The box has 32 GB and the deployed model uses 2.96 GB, so RAM is not the limit. The limit is memory
bandwidth: the deploy is bound at **~2.9 GB/token** and delivers **5.46 tok/s**, i.e.

```
effective bandwidth ≈ 2.9 GB/token × 5.46 tok/s ≈ 15.8 GB/s
```

which is consistent with dual-channel DDR4 on a 2700X under a 6-thread load.

**Therefore `tok/s ≈ bandwidth ÷ bytes-per-token`, and a bigger model costs speed LINEARLY:**

| class | Q4_K_M size | projected tok/s | 250-token answer |
|---|---|---|---|
| **Gemma 4 E2B (deployed)** | **2.96 GB** | **5.46** *(measured)* | **~46 s** |
| 7–8B | ~4.5 GB | ~3.5 | ~70 s |
| 14B | ~8 GB | ~2.0 | ~2 min |
| 27–32B | ~16 GB | ~1.0 | ~4 min |

**All of these FIT in RAM.** Speed is the currency, not memory. Projections are arithmetic from the
measured bandwidth — they are not measured, and a real bench-off would need to confirm them.

> **MEASURED 2026-07-27 — and this projection is WRONG in a way worth keeping.** `MODEL_BENCH_2026-07.md`
> benched three of these on the box. **File size is the wrong denominator.** Gemma 4 E4B is LARGER on
> disk than Llama 3.1 8B (5.03 vs 4.58 GB) yet runs **51% faster** (11.0 vs 7.3 tok/s), because Gemma 4's
> MatFormer/PLE architecture means *active parameters per token* — not file bytes — set the bandwidth
> cost. Measured ratios vs the incumbent: **E4B 0.54×, Llama 3.1 8B 0.36×**. The 7–8B row above predicted
> ~0.64×, i.e. it was **optimistic by ~1.8×** for that class. Use measured ratios, not size arithmetic,
> for any model whose architecture differs from the incumbent's.

---

## 2. The one option that breaks the linear trade

**Bonsai 27B** — PrismML, released 2026-07-14, Apache-2.0. Surfaced in the `2026-07-21` briefing.

- A **Qwen3.6-27B distilled to 1-bit binary + 1.58-bit ternary**
- **Runs in 3.9 GB** — roughly our current footprint, at 27B class
- Reported 11 tok/s on an iPhone 17 Pro
- Quality retention: **ternary >95%** of full precision, **1-bit >90%**
- Built on **Qwen3.6**, i.e. the Qwen family the engine already supports (we run Qwen3.5 DeltaNet SSM)

At 3.9 GB our bandwidth maths gives **~4.1 tok/s** — within a whisker of what we run today, for a
model two orders of magnitude larger in parameter count.

**Low-bit weights collapse the bandwidth wall rather than trading against it.** This is the shipped
end of the TurboQuant/RotorQuant lever deferred in the Phase-4 ADR.

### What blocks it — concretely

1. **No AVX2 kernel.** llama.cpp PR #21273 merged `Q1_0_g128` with **NEON + scalar only**; its own
   author noted the NEON paths were AI-generated and hand-tuned and the generic path is a scalar
   Q4_0 clone. **Not CPU-runnable on our x86-AVX2 deploy without a hand-rolled `qdot`.**
2. **That work is ours already, though.** We own the fused-AVX2-`qdot` path across seven quant types
   (`qdot.c`), so this is an extension of existing code rather than new territory.
3. **Load-time whitelist rejects it by design.** `qmodel_load`'s H2 gate whitelists
   F32/F16/BF16/Q4_0/Q8_0/Q4_K/Q5_K/Q6_K and `goto fail`s on anything else — correct behaviour, and a
   deliberate edit to extend.
4. **Qwen3.6 architecture delta unknown.** We support Qwen3 and Qwen3.5; the Qwen3.6 delta has not
   been assessed.
5. **Extreme-low-bit needs models TRAINED for it.** This is not a post-hoc requant of Gemma 4, and
   the format is `Q1_0_g128`-specific.

### Honest unknowns

- **>95% of a 27B ought to beat 100% of a 2B — but that is an inference, not a measurement.** It has
  to be benched on our own data before it justifies the kernel work.
- The 11 tok/s figure is Apple silicon with very different bandwidth; our ~4.1 is a projection.

---

## 3. Other candidates on the radar

From the briefing series, all unmeasured by us:

| model | note |
|---|---|
| **Kimi K3** | weights were pending self-hosting ~2026-07-27 |
| **Microsoft Aion-1.0-Instruct** | open weights "committed for July", unposted as of Jul 21 |
| **Ministral 3** | dense-reasoning contender, Jul 20 |
| **Qwen 3.5 / 3.6, Phi-4** | already-supported families, larger members |
| 7–8B general class | Llama 3.3 8B, Qwen 3 7B, Mistral Small 3, Granite 4.1 8B — all Q4_K_M ~4.5 GB, run on the **existing engine today** |

**The 7–8B row is the only option requiring no engine work at all.** It costs ~36% of current speed
for a modest quality gain — the least interesting trade on this page, but the cheapest to try.

---

## 4. The prerequisite nobody has done

**Re-bench under the deployed prompt shape, before choosing anything.**

The bench-off that selected Gemma 4 E2B (8.40/10, 7 blind judges) ran
`phase3/scripts/bench_quality_windows.ps1`:

```powershell
& $LlamaCLI -m $m.FullName -p $P -n 100 --temp 0 -ngl 99 -t 6 -no-cnv --no-warmup
```

`-no-cnv` **explicitly disables conversation mode**, so no chat template is applied — **raw
completion, not a chat turn.** Every score in that comparison measured a prompt shape the box has
never used, and any new model compared against it inherits the flaw.

**Attribution corrected 2026-07-27.** This first cited the Linux `phase3/scripts/bench_models.sh` and
inferred raw completion from the absence of `--jinja`. Both were wrong — the recorded results
(`ALL_RESPONSES.txt`, `04/09/2026`, `Device 0: NVIDIA GeForce RTX 2070`) come from the Windows GPU
harness, and `llama-cli` has auto-applied a chat template whenever the GGUF carries one since PR
#11214 (2025-01-13), so absent-`--jinja` never implied absent-template. **The conclusion survives on
better evidence:** the explicit `-no-cnv`, plus the recorded outputs continuing sentence fragments
(`"The seL4 microkernel is"` → `"a formally verified microkernel…"`), which is completion behaviour.
Full derivation, and the two traps any re-bench must avoid, in `THINKING_MODE_RESEARCH.md` §7.

**A model decision taken on that baseline is not defensible.** Fixing the harness is a prerequisite,
and it is cheap relative to porting a quant kernel.

---

## 5. Recommendation

**No model swap yet. In order:**

1. **Fix the bench harness** — measure under the real deployed prompt shape (template, chat turns,
   the actual generation cap).
2. **Re-bench the incumbent plus 2–3 candidates** on our own workload — which is now known: 30 unique
   questions, ~16 of them explicitly short-form definitional (`THINKING_MODE_RESEARCH.md` §2).
3. **Only then** decide whether the answer is a 7–8B (no engine work, linear speed cost) or Bonsai-class
   low-bit (an AVX2 kernel, but no speed cost and a category jump).

**The honest framing:** at fixed bandwidth the question is **quality per byte**, not parameter count.
A 7B at Q4 buys a modest gain for a third of the speed. A ternary 27B buys a category jump for a
kernel. Those are different kinds of decision and should not be compared on parameter count alone.

**Sources:** [PrismML — Bonsai 27B](https://prismml.com/news/prismml-releases-bonsai-27b) ·
[llama.cpp PR #21273](https://github.com/ggml-org/llama.cpp/pull/21273) ·
[Qwen3.6-27B benchmarks](https://kie.ai/blog/qwen-3-6-27b-deep-dive-benchmarks-quantization) ·
[Best SLMs under 10B, 2026](https://www.labellerr.com/blog/best-small-language-models-under-10b-parameters/) ·
internal: `briefings/2026-07-21-tech-briefing.md`
