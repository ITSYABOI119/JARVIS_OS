# Thinking Mode — research finding and verdict

**Date:** 2026-07-27 · **Status:** CLOSED — `JARVIS_THINKING` stays 0
**Scope:** Gemma 4 E2B (~2B effective params) on the deployed control-IN workload

---

## Verdict

**Keep thinking mode OFF.** Not deferred, not blocked — closed on convergent evidence from three
independent directions: our own measurements, the composition of the real workload, and published
evaluation of this model family at this size.

**Confidence: high for this model and this workload. This is NOT a claim that thinking modes don't
work** — see §5.

---

## 1. What we measured on the box

### The decisive run (`6cbfbba`) — verdicts pre-registered before execution

| | `THINKING=0` | `THINKING=1` |
|---|---|---|
| **Q1** (arithmetic: % of wall-clock lost to faults) | 496 tok, `MODEL-ENDED-TURN`, 1376 B — **CORRECT 5.56%, all five steps shown** | think=**651**, answer=249, `stop=CAP` — the thought reached 5.56%, then **the ANSWER restarted the whole calculation and was cut at "2 hours x 3". No result delivered** |
| **Q2** (trap: does a bandwidth-bound workload inherit a 50× matmul speedup?) | 866 tok, `MODEL-ENDED-TURN`, 3558 B — **TRAP AVOIDED**, reason named | think=**828**, answer=72, `stop=CAP` — cut at *"Your current system is explicitly \*\*memory-"*. **The verdict could not even be APPLIED to what was delivered** |

**Both OFF legs ended naturally and got both pre-registered verdicts right. Both ON legs hit the cap
and delivered nothing usable.**

### The mechanism — and it is not "small models reason badly"

**`THINKING=0` already emits visible chain-of-thought in the answer body.** The model shows its
working *in the answer*, where it is counted, delivered, and readable.

**Thinking mode does not replace that reasoning — it duplicates it.** The model works the problem
inside the channel, then works it again in the answer. So the mode costs **~2× for the same content**,
and because the thought is generated first, **the answer is what gets truncated**.

### The 320-token figure does NOT generalise

`3330923` measured a 320-token thought on a retrieval question and that number was used for sizing.
It is wrong as a general figure — **think length scales with difficulty:**

```
320 (retrieval)  ->  651 (arithmetic)  ->  828 (multi-factor)
```

**A fixed think budget therefore cannot be sized**, and the harder the question the more it starves
the answer — exactly backwards.

**D2 force-close is not a rescue.** Closing at ~150 tokens truncates a thought that needs 651–828
mid-reasoning, and `f1fc84b` already measured that a mid-clause context degrades the output that
follows. Force-close would manufacture the very condition that fix was written to eliminate.

### Cost, for completeness

598-token turns fit KV 1024 with the heap break unmoved. **Cost was never the problem.**

## 2. The workload — the empirical crux

Read from the dedicated control-IN store (`@21140000`): **66 turns, 30 unique questions.**

| class | count | examples |
|---|---|---|
| **Short-form definitional** | **~16** | *"in one line, what is a mutex"*, *"in one line, what is a translation lookaside buffer"*, *"explain paging in one line"* |
| Box state | 4 | *"how long have you been up?"*, *"what is your cpu usage?"* |
| Multi-step reasoning | 4 | the CPU-cores question and its variants |
| Unanswerable by design | 3 | *"what's my favorite color?"*, *"who do i like most"* |
| Test markers | ~4 | `JARVIS-MINIFLIP-7X` and friends |

**Sixteen of thirty questions explicitly request brevity — they literally begin "in one line".**
Re-derived 2026-07-27 and **exact**: 66 turns, 30 unique, 16 beginning `in one line` (17 containing
the phrase — `explain paging in one line` puts it at the end).

Published reasoning-mode gains concentrate on multi-step work; our traffic is short-form retrieval
where the operator has *asked for a single line*. Spending 320 tokens deliberating before answering
"in one line, what is a mutex" is the opposite of the request.

**CORRECTION — this section was labelled "the empirical crux" and that is too strong.** The counts are
right; the characterisation was not. **Of the 16 "in one line" openers, only 4 are genuine operator
questions.** The rest are our own synthetic traffic:

| kind | n | examples |
|---|---|---|
| generated filler | 8 | `in one line, os concept 0`, `in one line, describe operating system topic 1` |
| test-marker probes | 4 | `in one line, what is the JARVIS-MINIFLIP-7X reference value` |
| **genuine operator questions** | **4** | `in one line, what is a mutex`, `in one line, what is a translation lookaside buffer`, `in one line, what is a memory management unit`, `in one line, what does the seL4 capability system provide` |

Adding `explain paging in one line` gives **~5 of 30** genuinely operator-authored short-form
questions, not 16. The earlier caveat also called the whole set "operator-authored", which is **wrong**
— roughly two-thirds of the short-form set was generated by probe runs, including several of mine.

**What this does and does not change.** The verdict rests on three legs (§4); this is the weakest one
and it gets weaker. The measurement (§1) and the published evidence (§3) are untouched, and the
mechanism — thinking DUPLICATES reasoning the model already emits in the answer — does not depend on
workload composition at all. **The verdict stands; "the empirical crux" does not.** Treat §2 as
corroboration, and treat the real workload as *unknown and small* rather than as measured evidence
for short-form dominance.

---

## 3. Published evidence

### 3a. The vendor does not enable it here

- Google's Gemma 4 E2B usage example ships **`enable_thinking=False`**; thinking requires actively
  passing `enable_thinking=True`.
- The official thinking guide gives **no published thinking-vs-non-thinking deltas**, no token-budget
  guidance, and **no task-type guidance** about where it helps.
- **No vendor thinking/non-thinking split exists for E2B.** That is weak evidence in itself — a clear
  win at this size would be published.

### 3b. The headline numbers are not E2B's

The widely-quoted *"thinking makes Gemma 4 competitive — AIME 2026 at 89.2%, GPQA Diamond 84.3%"*
describes the **large** family members. **E2B's own card reports 37.5% on AIME 2026 and 60.0% MMLU
Pro.** The reasoning headline was never this model's.

### 3c. Independent evaluation says it *hurts* at this size

An on-device evaluation of this family measured:

- **GSM8K: thinking OFF 70% → thinking ON 55%** — a 15-point regression on the benchmark where
  chain-of-thought should help most
- Conclusion quoted: *"at 2B parameters, extended chain-of-thought introduces compounding errors
  rather than corrections"* — the small model "talked itself out of" correct answers
- Cost: **4–14× tokens**, **4.7× latency**

**Caveat, stated because it matters:** 20 samples per benchmark, a smoke test, and its HumanEval and
MMLU runs were invalidated by parser bugs. GSM8K is the only usable number. **Directional, not
conclusive on its own** — but it converges with our measurement and with a plausible mechanism.

### 3d. Stripping is a requirement, not our invention

The official docs state thinking **must be stripped between turns** (except during function calling).
Our D4 decision was right, and it remains **unimplemented** — a further reason the flag could not be
flipped today even if the verdict had gone the other way.

---

## 4. Why the three lines of evidence agree

Our measurement, the workload composition, and the published evaluation converge on one mechanism —
and it is **not** "a 2B model reasons badly".

**The model already reasons in the answer.** Chain-of-thought is present at `THINKING=0`, visible and
delivered. Thinking mode adds a *second* copy of that reasoning in a channel that is then discarded,
paying twice and truncating the half the operator actually receives.

That is consistent with the independent finding that CoT at 2B *"introduces compounding errors rather
than corrections"* — a second pass over the same reasoning is an opportunity to diverge, not to
verify. And it explains why the effect is worst on hard questions: the harder the problem, the longer
the duplicated thought, the less answer survives.

## 5. What this does NOT say

- **Not** "thinking modes don't work." They demonstrably do — at scale, on multi-step tasks.
- **Not** a claim about E4B, 12B, 26B-A4B or 31B.
- **Not** a claim about workloads with genuinely multi-step questions.

The operator's prior — *every frontier lab ships this, so why would ours be the exception* — was
correct reasoning. The answer is that **we are the exception because of size and task mix**, not
because the technique is unsound.

---

## 6. What would reopen it

1. **A larger or reasoning-trained model on the box.** This is the real form of the question: the
   89.2% AIME belongs to a model we do not run. Constrained by hardware — the deployed model is
   memory-bandwidth-bound at ~2.9 GB/token and delivers 5.46 tok/s.
2. **A GPU**, making the 4–14× token cost and 4.7× latency cheap.
3. **A workload shift** toward multi-step questions. Currently 4 of 30.

If any of these change, re-run the `6cbfbba` harness — the instrumentation (`[PB] gen … think= answer=
closer=`) is committed and probe-gated.

---

## 7. Surfaced by this research — needs its own investigation

**The bench-off that selected this model used no chat template. The CONCLUSION holds; the harness
first cited for it was the wrong script, and the correction matters — see the trap at the end.**

**The harness that actually produced the 8.40/10 is `phase3/scripts/bench_quality_windows.ps1`:**

```powershell
& $LlamaCLI -m $m.FullName -p $P -n 100 --temp 0 -ngl 99 -t 6 -no-cnv --no-warmup
```

`-no-cnv` **explicitly disables conversation mode**, so the template is never applied — raw
completion, not a chat turn. **Gemma 4 E2B's 8.40/10 therefore measured neither thinking nor the
model's own chat format**, which is what §7 originally claimed.

**Attribution corrected 2026-07-27.** This section first cited the Linux `phase3/scripts/bench_models.sh`
and inferred raw completion from the *absence* of `--jinja`. Both were wrong:

- **Wrong script.** `models/quality_results/ALL_RESPONSES.txt` is dated `04/09/2026` and every entry
  carries `Device 0: NVIDIA GeForce RTX 2070` — a Windows GPU run. `bench_models.sh` forces `-ngl 0`
  on the box. Three quality harnesses exist that neither doc mentioned (`bench_quality.sh`,
  `bench_quality_windows.ps1`, `bench_single_quality.ps1`); the Windows one matches the recorded
  device, thread count and date.
- **Wrong inference.** Absence of `--jinja` does NOT imply raw completion. `llama-cli` has
  auto-enabled conversation mode whenever the GGUF carries a chat template since **PR #11214
  (2025-01-13)** — fifteen months before this bench — via `completion.cpp`'s
  `COMMON_CONVERSATION_MODE_AUTO` branch, with `enable_chat_template = true` by default. The
  deployed GGUF *does* carry one (`tokenizer.chat_template`, 16,804 chars). The claim is true only
  because of the explicit `-no-cnv`, not because `--jinja` was omitted.

**Independent confirmation that does not depend on either script:** the recorded responses continue
sentence fragments. Prompt `"The seL4 microkernel is"` → `"a formally verified microkernel designed
for high-assurance systems…"`. That is completion behaviour; a chat turn would have answered rather
than continued the sentence. No recorded response contains a turn marker or thinking text.

**THE TRAP, and it is live for any re-bench** — verified empirically on the box against the
**deployed** `GEMMA2B.GUF` using the **bench-era** binary (`b8728-5e9c63546`, 2026-04-09):

1. **`bench_models.sh` today does the OPPOSITE of what it was cited for.** With no `-no-cnv`, it
   auto-enables conversation and applies Gemma 4's real template — *including* `<|think|>` in a
   leading system turn:
   ```
   <|turn>system
   <|think|><turn|>
   <|turn>user
   What is the difference between a process and a thread?<turn|>
   <|turn>model
   ```
   The model then emits `[Start thinking]`. **Re-running that script would silently measure
   thinking-ON** — the configuration §1 closed.
2. **`-no-cnv` no longer works.** The current binary answers `--no-conversation is not supported by
   llama-cli` and proceeds *with* conversation mode. The old harness cannot simply be re-run either.

So a re-bench must control templating **and** thinking explicitly, and verify what prompt was built
rather than trusting a flag. This strengthens the case for fixing the harness; it does not invalidate
the model choice — it means the evidence behind it measured a prompt shape the box has never used.

---

## 8. What survives from the arc

Closed as a negative result, but it produced:

- **`route.c` SYSFACTS bare-word defect** — found while authoring test questions; live on the
  deployed box, no keyword fix exists (`5e9d746`)
- **D4 stripping is unimplemented** — a documented decision that was never built
- **Slow generation is indistinguishable from a wedged PB** — a control-IN timeout feeds
  `km2b_miss_on_pb_timeout(KM2B_LANE_CTRL)`, and three restart Process B. The fix is a PB liveness
  tick, not a longer timeout
- **The generation cap, not transport, is what cuts answers today** — the model wanted 3558 B on a
  real question; the deployed 250-token cap delivers ~1225 B, silently
- **The bench-off methodology finding** above

**Sources:** [Thinking mode in Gemma — Google AI for Developers](https://ai.google.dev/gemma/docs/capabilities/thinking) ·
[google/gemma-4-E2B-it — Hugging Face](https://huggingface.co/google/gemma-4-E2B-it) ·
[Reasoning Mode Evaluation in On-Device Transformers (Gemma 4)](https://medium.com/@saloni_garg/reasoning-mode-evaluation-in-on-device-transformers-gemma-4-9ccc9762a6df) ·
[gemma-4-benchmarks](https://github.com/lmassaron/gemma-4-benchmarks)
