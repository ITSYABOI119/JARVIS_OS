# Answer Quality — separate think/answer budgets + a thinking toggle: RESEARCH + DESIGN

**Status: PLAN-FIRST. No code, no flag, no wire change.** Authored 2026-07-26 after the boot-40
bare-metal validation of image `9362c756` came back with thought-channel output on a FRESH question.

**Read §1 first.** The research changed the problem. Three of the five design questions below are
smaller than they looked, and two defects were found that no amount of budget tuning would fix.

---

## 0. The correction that got us here

The recall fix (`f1fc84b`) was diagnosed from a controlled experiment: question held fixed, preamble
varied, byte-identical reproduction of both the good and the bad box output. That experiment was
valid and its finding is real — **a mid-sentence preamble does change the outcome**. The conclusion
drawn from it was one layer too shallow.

The operator's question 3 — *"what's my favorite color?"* — is FRESH. No exact-key match, no
preamble possible, recall cannot have fired. It produced thought-channel output anyway. So the
preamble is a **modulator of how long the model thinks**, not the trigger. The recall fix stays (it
removes one source of over-thinking, and injecting a mid-clause fragment is wrong regardless), but
it was treating a symptom.

---

## 1. RESEARCH — ground truth from the model file

Read directly out of the deployed model, `models/gemma-4-E2B-it-Q4_K_M.gguf` (the same GGUF that is
on the box's `JARVIS_DATA` as `GEMMA2B.GUF`).

### 1.1 The hand-rolled token ids are ALL CORRECT

Verified against `tokenizer.ggml.tokens`:

| id | token | `inference_server.c` claim | verdict |
|---|---|---|---|
| 1 | `<eos>` | `<eos>` | correct |
| 2 | `<bos>` | `<bos>` | correct |
| **98** | **`<\|think\|>`** | `<\|think\|>` | **correct** |
| 100 | `<\|channel>` | *(not used)* | — |
| 101 | `<channel\|>` | *(not used)* | — |
| 105 | `<\|turn>` | `<\|turn>` | correct |
| 106 | `<turn\|>` | `<turn\|>` | correct |
| 107 | `\n` | `\n` | correct |
| 2364 | `user` | `user` | correct |
| 4368 | `model` | `model` | correct |

**So the premise "maybe token 98 isn't a think token" is dead — it is exactly what the code says it
is, and this IS a thinking-capable model.** The bug is not the id. It is *where the token is put*.

### 1.2 The model's own chat template — and the divergence

`tokenizer.chat_template` EXISTS in the GGUF. `gguf_vocab.c` reads only `bos_token_id` and
`eos_token_id` (`:187`, `:193`) — **the template has never been read or compared against.** The
full template is ~300 lines, mostly tool-calling macros. The load-bearing parts, verbatim:

```jinja
{%- if (enable_thinking is defined and enable_thinking) or tools or messages[0]['role'] in ['system', 'developer'] -%}
    {{- '<|turn>system\n' -}}
    {#- Inject Thinking token at the very top of the FIRST system turn -#}
    {%- if enable_thinking is defined and enable_thinking -%}
        {{- '<|think|>\n' -}}
        {%- set ns.prev_message_type = 'think' -%}
    {%- endif -%}
    ...
    {{- '<turn|>\n' -}}
{%- endif %}
```

```jinja
{%- if add_generation_prompt -%}
    {%- if ns.prev_message_type != 'tool_response' and ns.prev_message_type != 'tool_call' -%}
        {{- '<|turn>model\n' -}}
    {%- endif -%}
{%- endif -%}
```

**THE DIVERGENCE, and it is the root cause:**

| | model's own template | `inference_server.c:287-429` |
|---|---|---|
| `<\|think\|>` placement | inside a **leading `<\|turn>system … <turn\|>` block**, before the user turn | **appended LAST**, immediately after `<\|turn>model\n` |
| gated by | `enable_thinking` | **nothing — unconditional** |
| generation prompt | bare `<\|turn>model\n` | `<\|turn>model\n` **+ `<\|think\|>`** |

The deployed prompt ends by handing the model a think token **at the position where the answer is
supposed to begin** — a position the template never produces. The model responds the only sensible
way: it opens a thought channel. That is why *every* question, fresh or repeated, can produce
`<|channel>thought`.

**Corroboration from the bench-off corpus.** The 8.40/10 quality score was produced by **llama.cpp
on the RTX 2070 using the model's own template** (`models/quality_results/gemma-4-E2B-it-Q4_K_M.txt`
— llama-cli sampler dumps, `[end of text]`). That file contains **zero** occurrences of
channel/thought/think across all 10 prompts; the answers are direct. Across the whole
`models/quality_results/` tree the ONLY file containing `<|channel>` is
`qat_google_q4_0_responses.txt` — a **JARVIS-engine** run (identifiable by its `<turn|>` markers) —
and it emits the identical `<|channel>thought / Here's a thinking process` pattern the box produces.

So the thinking channel correlates with **our prompt**, not with the model.

### 1.3 THE THOUGHT BOUNDARY EXISTS — item (4) answered

This was the gating question and the answer is yes. The template defines the wrapper explicitly:

```jinja
{{- '<|channel>thought\n' + thinking_text + '\n<channel|>' -}}
```

and strips it by splitting on the closer:

```jinja
{%- macro strip_thinking(text) -%}
    {%- for part in text.split('<channel|>') -%}
        {%- if '<|channel>' in part -%}
            {%- set ns.result = ns.result + part.split('<|channel>')[0] -%}
```

**A thought is `<|channel>`(100) `thought\n` … `\n` `<channel|>`(101). The END is token 101.**
Separate think/answer budgets are therefore implementable: generate under the think budget until
token 101, then switch to the answer budget. There is a real boundary to switch on.

### 1.4 BONUS DEFECT — the generation loop stops on the WRONG token

`tokenizer.ggml.eos_token_id` = **106** (`<turn|>`). `inference_server.c:500` stops only on token
**1** (`<eos>`), and its comment says so deliberately: *"Stop on <eos>=1, NOT eos_id=106 (that's
<turn|> which model emits first)."*

That comment has it backwards. `<turn|>` is the model's declared end-of-turn — **emitting it IS the
model saying it is finished.** Ignoring it means the loop keeps generating past a completed answer
until it hits token 1 or the cap, and that padding is what puts `<turn|><turn|><turn|><eos>` into the
episodic store and the console reply.

> **CORRECTED IN PLACE 2026-07-26 — this section originally claimed "23 consecutive `<turn|>` tokens
> … 46% of the budget spent on padding", generalised from a single observation. THAT FIGURE IS
> WRONG and §11.1 supersedes it.** It came from ONE question **with a preamble injected**, which made
> the model finish early and then pad. Measured across 5 realistic questions with no preamble, **4 of
> 5 emit no terminator at all** (the model never finishes inside 50 tokens, so there is nothing to
> pad) and the one that does recovers 4 tokens — **real recovery is 0–8%, and the effective budget
> was ~50 all along.**

**Implication, as corrected.** The stop-token fix is still the right change — it is the correct
behaviour and it stops writing terminator junk into the store and the reply — but it is **not** a
budget fix, and the "we are wasting the budget we have" reading above does not hold. The opposite is
true: 5 of 5 answers are cut mid-sentence at the cap, so the budget is the binding constraint. See
§11.1 and §11.3.

### 1.4b RESIDUAL DEFECT IN THE SHIPPED RECALL FIX — the store is poisoned, and recall re-injects it

Read off the box after the failed validation (durable NVMe log, **all 2700 entries are boot_id=41**,
the boot that followed the `9362c756` deploy — the log had wrapped, evicting every earlier boot):

```
[41:1903713] IPC_STATS  [CTRL-IN-STATS] acc=3 drop=6 (parse=6 rl=0 auth=0 replay=0) bp=0 down=0 recall=2
```

`acc=3` matches the operator's three questions. **`recall=2` — but only ONE of the three should have
recalled** (the marker question; the CPU-cores key was predicted to suppress, and the colour question
is fresh). The extra one was found by running the shipped fixed builder against the real stored
bytes of slot 45 — *the thought-channel garbage record itself*, which `epi_index_lookup`'s
newest-wins had made the newest entry for that key:

```
Notes from a previous answer (use as reference; add new detail, do not repeat):
<|channel>thought
Here's a thinking process that leads to the suggested answer:

1.
```

**164 bytes, non-empty.** Two independent failures:

1. **`"1."` parses as a completed sentence**, so the complete-sentence rule does not suppress it.
   Enumerated text defeats the rule.
2. **Thought-channel text is in the store at all**, so recall injects the literal string
   `<|channel>thought` into the next prompt — the strongest possible nudge toward more thought
   output. It is a self-reinforcing loop: thought garbage is stored, recalled, and produces more
   thought garbage.

This is a defect in the fix I shipped, not a pre-existing one, and it is the mechanism behind
`recall=2`. **It also makes D4's "strip the thought before storing" load-bearing rather than a
nicety** — with the thought stripped at the source, neither failure above is reachable.

### 1.5 What this means

The honest summary is that the thinking path is **misimplemented, not misused**:

- Thinking OFF = simply do not append token 98. The prompt then matches the model's own
  `add_generation_prompt` output exactly, and behaviour should match the llama.cpp bench-off —
  direct answers, no channel.
- Thinking ON = emit `<|turn>system\n<|think|>\n<turn|>\n` **before the user turn**, as the template
  does, and budget the `<|channel>…<channel|>` block separately from the answer.

---

## 2. D1 — latency

**Measured inputs, not assumed:**

- Live throughput, from the box's own JACT status digests this session: `tok=541 / 542 / 545 / 548 /
  560` → **5.41–5.60 tok/s**. Consistent with the 5.46 benchmark.
- Control-IN poll budget: `poll_max = 5000000` spin-iterations (`main_x86.c:3151`), documented at
  `:3050` as **"~60-120 s"**. **This is a RANGE IN A COMMENT, not a measurement.**

| config | tokens | time @5.46 tok/s |
|---|---|---|
| today | 50 combined | ~9 s |
| answer-only 192 | 192 | ~35 s |
| think 128 + answer 192 | 320 | ~59 s |
| the "200/200" illustration | 400 | **~73 s** |

**Finding: 400 tokens fits at the 120 s end of the poll budget and BREAKS at the 60 s end.** The
budget is an iteration count whose wall-time nobody has measured; sizing a 73 s inference against it
is exactly the kind of assumption that has bitten this project twice this week.

**Recommendation.**
1. **Measure `poll_max`'s real wall-time first** (cheap: one KVM run, timestamp the poll loop). Do
   not size budgets against a comment.
2. Then set `poll_max` **explicitly** from the chosen budget (`answer_tokens / tok_s × safety`),
   rather than inheriting the wake lane's number by accident.
3. Land §1.4's stop-token fix first, so the budget is spent on content.

**A UX point the operator should decide with real numbers in hand:** thinking ON at these caps means
**~1 minute per answer**. Thinking OFF at answer-192 is ~35 s. The operator prefers thinking ON;
that is a legitimate choice, but it is a 6–7× latency increase over today and should be a decision,
not a side effect.

## 3. D2 — transport

> **CORRECTED AT IMPLEMENTATION 2026-07-26 — there are SIX ceilings, not three, and the MTU number
> below was wrong.** The missing one is **`char resp[1024]` in `pa_ctrl_gate`** (`main_x86.c`, both
> the ROUTING and `!ROUTING` paths) — PA's own chunk accumulator, which would have clamped a
> 1426-byte answer to 1023 *before* `ctrl_send_reply` ever saw it. Raising any subset just moves
> where the answer is cut. **The SIXTH is `int output_ids[64]` in `handle_query`** — a STACK array
> holding the generated tokens. It is the one nobody lists, and the only one that did not merely
> risk corruption but actively corrupted (see §12).
>
> **And the MTU arithmetic was misattributed as well as wrong.** Precisely: the MTU is 1500 (the
> maximum IP DATAGRAM — the Ethernet header is NOT subtracted from it). The CONSTANT in the code is
> **`UDP_MAX_PAYLOAD = 1472`** (`net_udp.c:25` = `1500 - IP_HDR_LEN - UDP_HDR_LEN`). **1426 is
> neither of those** — it is the max *TEXT*, i.e. `UDP_MAX_PAYLOAD 1472 − 46 JRPL overhead`
> (10 hdr + 4 crc + 32 tag). The earlier ~1412 subtracted the Ethernet header twice.
> The frame on the wire is 1514, inside the driver's `I211_MAX_FRAME_SIZE` 1536. All five now move
> together, four of them DERIVED from `CTRL_REPLY_TEXT_MAX` so they cannot drift again, and
> `test_control_reply.c` T7 pins `CTRL_REPLY_MAX_LEN <= 1472` so a future bump fails the build
> rather than producing a frame the NIC will not send.

**Three ceilings, not one.** All three bite before 200 answer tokens (~800 bytes):

| ceiling | value | where | at 192 answer tokens |
|---|---|---|---|
| PB decode buffer | `text_out[512]` | `inference_server.c:541` | **overflows** |
| reply text | `CTRL_REPLY_TEXT_MAX 512` | `control_reply.h:60` | **clamped** |
| ring chunking | 15 slots × 240 B | `shmem_ipc.h:18,20` | 4 chunks — fits |

And the failure is **silent**: the chunk loop does `(void)rc` then `offset += chunk` regardless
(`inference_server.c:562-578`), while `shmem_ipc_send` returns −1 on a full ring. Today that is
latent only because 512 B can never exceed 3 chunks.

**What has to grow, and the cost:**
- `text_out[512] → [1024]`: +512 B on `handle_query`'s frame. The documented budget is <8 KB and
  the current frame is well under; needs a re-check, not a redesign.
- `CTRL_REPLY_TEXT_MAX 512 → 1024`: `CTRL_REPLY_MAX_LEN` 558 → 1070, so the `reply_frame` buffer and
  its `_Static_assert(558+42 <= 640)` must grow to ~1152. Total UDP frame ~1112 B — **still inside a
  1500 B MTU**, so no fragmentation and no new failure mode on the wire.
- **Fix the discarded `rc`** in the same change. Growing the payload without checking the return
  turns a latent silent-drop into a reachable one.

**Recommendation: do NOT put the thought on the wire in v1** (see D4). That keeps the answer the only
thing that has to fit, and 1024 B covers a 192-token answer with margin.

## 4. D3 — which lanes? (the brief's premise is wrong)

The brief states *"~14% of 12.2M queries are real inferences"*. **Measured, it is ~0.1%:**

- KVM gate this session: `[STATS] q=14,700 hits=12,451 infer=20` → **0.14%**.
- CLAUDE.md, #6/M3 bare metal: *"infer FROZEN at 17 while q reached 283,400"* → **0.006%**.

Cache growth serves essentially the whole synthetic workload; real inferences are rare. So the
*cost* argument for keeping the workload lane modest is much weaker than assumed.

**Recommendation: per-lane caps anyway — control-IN generous, workload stays at 50 — but for a
different and better reason than cost.** The workload lane exists to generate load and to produce
the `infer_last_tok_x100` figure; every historical benchmark, telemetry series and soak baseline was
produced at 50 tokens. Changing it silently re-bases all of them. Keep it at 50 for comparability,
and give control-IN — the lane a human actually reads — the real budget.

**Bare-metal, boot 41** (durable log): `q=474200 hit=403206 err=0`. The durable `IPC_STATS` format
does **not** carry an `infer` counter, so this is an ESTIMATE, not a measurement: the 70,994
non-hits are `infer + hb + shield`, and applying the KVM lane ratios (hb ≈ 10% of q, shield ≈ 4.6%)
leaves **infer ≈ 1,900 ≈ 0.4% of q**. Same order as the other two figures, still ~35× below the
brief's 14%. An exact bare-metal number needs `q_infer` off the v12 telemetry during a JARVIS boot;
it is not worth a boot on its own, and three independent estimates already settle the premise.

## 5. D4 — is the thought shown?

**Recommendation: hidden in v1, but COUNTED and surfaced as a number.**

- Sending the thought costs wire budget (D2) for text the operator mostly does not want.
- The model's own template strips it (`strip_thinking`) when re-encoding history — the model itself
  treats thought as non-durable. Storing it in the episodic/control-IN store would also pollute the
  recall corpus with reasoning scratch.
- But "thinking is on and I cannot tell" is exactly the visibility complaint in §7. So report
  `think_tokens` as a field: the console can say *"thought for 96 tokens"* without carrying them.
- Full thought text behind a console toggle is a clean v2 once the field exists.

**Corollary: the thought must be stripped before the answer is stored and replied.** With the §1.3
boundary this is deterministic — drop everything up to and including token 101.

## 6. D5 — toggle mechanism

**Recommendation: compile-time `JARVIS_THINKING`, default-ON, per project convention** — matching
`JARVIS_ROUTING` / `JARVIS_CONTROL_IN` / every other capability flag, with the OFF build compiling
the system-turn emission out entirely.

Runtime/per-query is deferrable and, notably, **the model supports it** (`enable_thinking` is a
template parameter). A per-query toggle would need a control-IN command verb or a JCTL field — that
is a 6-5-shaped change with its own security surface (a new inbound field is new untrusted input),
and it should not ride this milestone.

---

## 7. Surfacing the answer's SOURCE

With routing live there are three sources — SYSFACTS (box state), DECLINE (canned), INFER (Gemma) —
and the reply does not say which. The console therefore hedges: *"one bounded inference or a cache
hit on the box"*. The operator cannot tell whether the model spoke.

**The box already knows.** At all four `ctrl_send_reply` call sites, `rc` (the `route_class_t`) and
`served_locally` are in scope (`main_x86.c:2991, 3028, 3036, 3235`). Nothing needs to be computed —
only carried.

**JRPL v2's 10-byte header is fully allocated** (`control_reply.h:23-33`) — magic/version/verdict/
seq/tlen, no spare. So this needs **JRPL v3**, in receiver+console lockstep (the receiver
deliberately has no version fallback, by the M4b anti-downgrade rule).

**Recommendation: one v3 bump carrying BOTH new fields** — `route` (u8) and `think_tokens` (u16) —
rather than two lockstep changes a milestone apart. Both are small, both are needed for the same
validation task ("is this the model talking, and did it think?"), and the version-bump cost is
almost entirely fixed overhead.

Honesty constraint for the console: `route` is the **classifier's decision**, not proof of what
produced the text. A SYSFACTS reply is genuinely rendered from box state; an INFER reply genuinely
came from Gemma. But the label must not be rendered as a quality claim.

---

## 8. Milestone split

**M0 — host, no box.** The thought-boundary scanner and the answer/thought split as pure functions
(the `km2b_trigger.c` / `wake.c` precedent): given a token stream, find token 101, split, strip.
Host-tested + a CI step. Also the stop-token predicate (§1.4), and **the §1.4b recall-time guard: a
candidate whose text contains a thought marker is not recallable.**

That guard is needed *in addition to* stripping at the source, because **the poisoned records are
already in the store**. The control-IN store is circular with 4096 slots and control-IN is
human-paced, so at the observed rate those records would take **years** to age out. Options are: a
recall-time filter (cheap, pure, host-testable — recommended), or zeroing the control-IN region
(destructive, throws away the genuine history with it), or waiting them out (not viable). The filter
also protects against any future record that slips through.

**M1 — the template correction + the stop-token fix, box, gated.** Move `<|think|>` from the tail to
a leading `<|turn>system\n<|think|>\n<turn|>\n` block behind `JARVIS_THINKING`; stop on `<turn|>`
(106) as well as `<eos>`. **This milestone alone may resolve the reported defect** — it is worth
gating and measuring *before* any budget change, precisely because it is the change that makes the
prompt well-formed. Gate: a fresh question returns prose, not a channel; `<turn|>` padding gone.

**M2 — separate budgets.** `think_max` / `answer_max`, sized from the D1 measurement, with the
poll budget set explicitly. Force-close the channel if the think budget is exhausted so an answer is
always produced.

**M3 — transport (D2).** `text_out` and `CTRL_REPLY_TEXT_MAX` growth, the `_Static_assert` update,
and the discarded-`rc` fix.

**M4 — JRPL v3**: `route` + `think_tokens`, receiver, console. Includes the §7 source display.

**M5 — flip** `JARVIS_THINKING` default-ON after a bare-metal validation.

Ordering rationale: M1 is first because it is the only one that fixes a **malformed prompt**; every
other milestone is sizing or plumbing around a prompt that should be correct first.

---

## 9. Honest ceiling

- Bigger budgets do not make the model better; they stop truncating it. The 8.40/10 bench-off figure
  was produced by llama.cpp on a GPU with the model's own template — matching that prompt shape is
  what this work is chasing, and matching it is **not** the same as reproducing that score.
- Nothing here has been demonstrated on seL4 yet. §1 is model-file and corpus evidence; §1.4's 46%
  padding figure is from a native-engine probe on the box, not from the deployed image.
- The `poll_max` wall-time is still a comment, not a measurement. Every latency number in D1 is
  arithmetic on a measured tok/s, not an observed end-to-end answer time.

## 10. Open questions for the strategist

1. **Does M1 alone fix it?** My prior is that it substantially does, and that budgets are then a
   quality knob rather than a bug fix. Recommend gating M1 alone and re-testing the three failing
   questions before committing to M2's numbers.
2. **Thinking default ON at ~1 min/answer, or OFF at ~35 s?** The operator asked for ON. I recommend
   shipping the toggle with ON as the default **only after** M1+M2 show the real end-to-end time on
   the box — if it lands near 90 s, that is a different product than they asked for.
3. **Workload lane at 50 for comparability** (§4) — confirm that re-basing the historical tok/s
   series is unacceptable, which is my assumption.
4. **One v3 bump for `route` + `think_tokens`** (§7), or route now and think later?
5. Should the §1.4 stop-token fix be split out and shipped on its own? It is small, it is a pure
   improvement, and it is independent of the thinking design. My prior: yes, fold it into M1 but
   gate it separately so a regression is attributable.
6. **The already-poisoned store (§1.4b).** Recall-time filter (my recommendation), or zero the
   control-IN region and lose the genuine history with it? The filter is pure, host-testable and
   protects future records too; zeroing is a one-off that also discards the marker-question lineage
   the recall gates depend on. I do not recommend zeroing.
7. **Was the recall fix worth shipping given §1.4b?** My view: yes — it is strictly better than
   injecting mid-clause fragments, and the KVM gate showed it discriminating correctly (6 of 14 hits
   still injected). But it was sized against a diagnosis that has since been superseded, and it did
   not survive contact with a store containing thought text. That is worth saying plainly rather
   than leaving the earlier "fix verified" framing standing unqualified.

---

## 11. RESULT (2026-07-26) — commits 1–3 landed, and BOTH orderings' premises died

Three commits, measured between each as instructed. Two premises this document and its brief were
built on turned out to be wrong, both killed by measurement rather than argument.

### 11.1 Commit 1 (stop-token) — the effective budget is ~50, NOT ~27

The ruling ordered this first on the basis that "your probe found 23 of 50 tokens were `<turn|>`
padding, so the effective budget is ~27, not 50" and was "about to move by ~2×". **Measured, over 5
realistic questions with no preamble:**

| question | terminators in stream | padding recovered |
|---|---|---|
| CPU cores | **0** | 0 (0%) |
| miniflip | 4 | 4 (8%) |
| favourite colour | **0** | 0 (0%) |
| page fault | **0** | 0 (0%) |
| artificial intelligence | **0** | 0 (0%) |

**4 of 5 emit no terminator at all.** The 23-token figure came from ONE question **with a preamble
injected**, which made the model finish early and then pad — it does not generalise. Real recovery
is **0–8%**, and the effective budget was ~50 all along.

**THE INVERSE FINDING IS THE IMPORTANT ONE: 5 of 5 hit the 50-token cap**, every answer truncated
mid-sentence ("…determined is **not currently"). Commit 1 is still correct — it stops writing
`<turn|><turn|><eos>` into the store and the reply, and it matters whenever the model does finish —
but **it is not a budget fix.**

### 11.2 Commit 2 (placement) — the CORRECT placement is WORSE

Native probe, 3 questions × 3 placements, then confirmed on seL4:

| placement | native (3 questions) | seL4 KVM (13 inferences) |
|---|---|---|
| trailing `<\|think\|>` (what shipped) | thought on 1 of 3 | — |
| **leading system turn (the model's own template)** | **thought on 3 of 3** | **every `[INFER]`, 15 occurrences** |
| no think token (`JARVIS_THINKING=0`) | clean answers on 3 of 3 | **0 occurrences**, coherent prose |

Both box gates: `err=0`, 0 MODEL-BAD/FATAL, workload counters identical
(`q=100 hits=71 infer=13 hb=11 shield=5`).

Correct placement makes the model think **more** — unsurprising once seen, because that placement is
exactly how the model was trained to be told "you are in thinking mode". So the flag ships
**default OFF**, deviating from the "Default: ON" ruling, because:

**Thinking ON + thought stripped + a 50-token SHARED budget = an EMPTY reply.** At the correct
placement the model spends all 50 tokens on thought; strip it (the agreed D4 behaviour) and there is
nothing left to send. ON is not a preference we can honour yet — it is blocked on arithmetic.

### 11.3 Is the budget milestone still needed? YES — it is now the main event

The brief hoped commit 1 + correct placement might make 50 tokens sufficient, which would have made
D1–D5 mostly moot. The measurement says the opposite:

- commit 1 recovers 0–8%, not 46%;
- 5 of 5 answers are cut mid-sentence at 50 tokens **with thinking off**, which is the deployed
  configuration;
- thinking ON is *gated on* the budget work rather than independent of it.

**So M2 (separate budgets) is promoted from "quality knob" to the critical path**, and D1's
unmeasured `poll_max` wall-time is now the first thing to measure, not a footnote. Revised ordering:

- **M0/M1 — DONE** (commits 1–3 below).
- **M2 — separate budgets + the poll_max measurement.** Now the blocking milestone. Note the
  answer-only case needs no thought budget at all: at `JARVIS_THINKING=0` a single raised
  `answer_max` is the whole change, and ~192 tokens ≈ 35 s is comfortably inside even the pessimistic
  60 s read of the poll budget.
- **M3 — transport (D2)**, unchanged, and it gates M2's usable ceiling at ~1024 B.
- **M4 — JRPL v3** (`route` + `think_tokens`), unchanged.
- **M5 — the `JARVIS_THINKING` flip**, which is now explicitly *after* M2, not before.

### 11.4 What landed

| commit | what | gate |
|---|---|---|
| `9d70f7f` | stop on the model's DECLARED `eos_token_id` (loaded, not hardcoded), break before storing | KVM `q=100 err=0`, coherent, 0 `turn\|`/`<eos>` vs 1 visible pre-fix |
| `d8993ed` | `<\|think\|>` moved to the leading system turn; `JARVIS_THINKING` **default 0** | KVM both states, `err=0` each |
| `c65e1aa` | thought scratch not recallable + line-leading `"N."` is not a sentence end | host 72 → **83 PASS**, gcc `-Wall -Werror` |

### 11.5 Honest limits

- The per-question token counts are from the **native engine**, not the deployed seL4 build. That
  harness reproduced box output byte-for-byte on both the good and the bad case, which is why it is
  trusted here — but it is corroboration, not the deployed path.
- **`poll_max`'s wall-time is still unmeasured.** Every latency number in D1 remains arithmetic on a
  measured tok/s, not an observed end-to-end answer time. M2 must measure it first.
- **Nothing is deployed.** The box still runs `9362c756` with the validation defect live.

---

## 12. M2 RESULT (2026-07-26) — transport + per-lane budget landed

Three commits, ordered transport-first so no new failure could fail quietly.

**Commit 1 — the silent hole.** The chunk loop advanced `offset` even when `shmem_ipc_send` returned
−1, so a full ring punched a **hole** in the answer with no error and no log. Fixed by retrying the
same offset with back-pressure (signal + yield; PA polls this ring and shares the core), advancing
only after a send that succeeded, and failing loudly on exhaustion via `puts_serial` — never
`pb_log`, which sends over the very ring that is full. Both halves gated with `JARVIS_RING_PROBE`
because **the branch had never executed**: mode 1 (fail the first 3 sends) recovered with the answer
intact, mode 2 (fail every send) produced
`[PB] RESPONSE TRUNCATED: … undelivered bytes=263 (answer is short but CONTIGUOUS — no hole)` and
0 FATAL.

**Commit 2 — six ceilings, not three; `UDP_MAX_PAYLOAD` is 1472 and the max TEXT is 1426.** See §3's inline correction. The one
the brief's table missed was `char resp[1024]` in `pa_ctrl_gate` — PA's own accumulator, which would
have clamped a 1426 B answer to 1023 before the reply builder saw it. Four of the five are now
DERIVED from `CTRL_REPLY_TEXT_MAX`. The receiver had to move in lockstep because it **rejects** a
larger `tlen` outright — a box-only bump would have made long answers vanish rather than truncate.

**Commit 3 — per-lane cap, and a SIXTH limit that was actively corrupting.**

`MSG_QUERY_LONG` 0x13 carries the lane, because PB cannot otherwise distinguish control-IN from the
workload. Workload stays at **50** for comparability with the entire historical performance record;
control-IN gets **250**, derived from the MTU at the WORST measured byte/token density
(250 × 5.66 = 1415 ≤ 1426 — a cap chosen from the mean would overflow the frame) and sized so its
~46 s worst case fits under the PESSIMISTIC end of the unmeasured poll budget.

**`int output_ids[64]` was the sixth limit**, and finding it is the reason the gate is worth the
time. At cap 250 a 65-token answer ran off a stack array. It did not merely risk corruption — it
*was* corrupting: the box returned 219 bytes with the stop-reason probe reporting
`n=50 stop=MODEL-ENDED-TURN`, i.e. the overflow was corrupting adjacent stack state and
**masquerading as the model ending its turn at exactly the old 50-token cap.** That is a coincidence
convincing enough that it was nearly written up as a model behaviour, and it would have quietly
contradicted commit 1's risk analysis in the previous milestone. Sized from the cap and made static,
the same query on the same image returns `n=250 prompt=15 stop=CAP` and **1138 bytes**.

### Measured

| | before | after |
|---|---|---|
| control-IN answer | 219 B | **1138 B** (5.2×) |
| tokens generated | 50 (corrupt) | **250**, `stop=CAP` |
| short answer ("explain paging in one line") | — | **27 tokens**, `stop=MODEL-ENDED-TURN` |
| workload lane | `q=100 hits=71 infer=13` | **identical** |
| TRUNCATED / FATAL / MODEL-BAD / ANOMALY | — | **0**, `err=0` |

The cap is a ceiling, not a fixed cost: a short answer still returns in seconds.

### Does this make JARVIS_THINKING ON viable?

**Arithmetically yes, and it is no longer blocked — but do not flip it on these numbers alone.** The
250-token budget is enough for a thought plus an answer only if the thought is short, and §11.2
measured the correct placement producing thought on 3 of 3 questions. What is still unmeasured is
**how many tokens a real thought consumes**, and with the thought stripped, every token it takes is
one the answer does not get. The honest next step is to measure thought length at
`JARVIS_THINKING=1` with the new budget before deciding whether 250 is a think+answer budget or only
an answer budget. That is a separate decision, as instructed.

### Honest limits

- The 250 cap is sized against a byte/token density measured over **five** answers. A denser answer
  than any observed would still be clamped by `CTRL_REPLY_TEXT_MAX` — correctly, and now visibly.
- `poll_max`'s wall-time is **still unmeasured**. The budget was sized to fit its pessimistic
  reading rather than to justify a larger one.
- The `[PB] lane` / `[PB] gen` instrumentation is probe-gated; a deployed build prints nothing, so
  the stop reason is not observable in production. **CLOSED at M3 — see §13.** The stop reason now
  leaves PB on the existing `MSG_INFER_STATS` channel and reaches the operator as reply verdict 4,
  so it is observable in production **on the wire**, which is where it matters, rather than in a log
  a deployed build never writes.

---

## 13. M3 (2026-07-28) — #18 front-loading, and #9 an answer that says when it was cut

Two changes, one commit. Both are **control-IN only**; the workload lane is untouched.

### 13.1 #18 — the front-loading instruction

`"Answer the question directly in your first sentence."` (~10 tokens), injected into the control-IN
prompt in the slot the retrieval preamble already uses.

**Why a prompt change and not a model swap.** The re-benched deficit (`4274ca4`) was answer
**ORDER**, not answer quality: this model preambles before answering, so at a finite cap the cut
lands on the substance. That is an interaction between the model's STYLE and OUR cap — not a model
defect — which is why the fix belongs in the prompt. Measured blind, same rubric same run:
**E2B 9.0 vs Llama 3.1 8B 7.8**, E2B baseline 4.3. The incumbent beats the nominal bench winner, so
**no model swap is happening** and #17 (Bonsai low-bit) is deprioritised rather than cancelled.

**Exactly one clause.** The two-clause variant (`", then elaborate."`) was measured NET-HARMFUL in
`1df800a` — `"then elaborate"` overrode `"in one line"`, driving control length-violations 0.0 → 7.0
and quality 8.0 → 4.0.

**Four implementation findings, each of which would have been a defect:**

1. **`handle_query` does not know its lane.** It took `max_tokens` only; the lane is resolved in the
   caller. Testing `max_tokens == 250` would have been a coincidence, not a fact — the day the two
   caps matched it would silently have started front-loading the WORKLOAD and re-based the 5.46
   tok/s record with no error anywhere. The lane is now an **explicit parameter**, and both it and
   the cap derive from one `msg_type` test.
2. **The g3-preamble slot is triple-gated** (`#if JARVIS_G3_RETRIEVAL` / `if (g_sctx_pb)` /
   `pre_len > 0`). An instruction placed *in* that block would vanish on every retrieval miss —
   which is common by design, since the complete-sentence rule suppresses fragments — and vanish
   entirely in a `JARVIS_G3_RETRIEVAL=0` build. It is emitted as a **separate unconditional
   injection at the same position**.
3. **The token budget could overflow.** `g3_prompt_budget` derives room from `n_prompt` *at the
   moment it is called*, and the instruction is appended after it, so the preamble could be granted
   room the instruction then consumed. The budget call now reserves for it.
4. **`G3_SUFFIX_TOKS` was the wrong constant to reuse** — `g3_retrieval.h` is included only under
   `#if JARVIS_G3_RETRIEVAL`, so the bound check would not have compiled in a G3=0 build. That is
   finding (2)'s mistake repeated one level down. A lane-independent `PB_TEMPLATE_SUFFIX_TOKS` now
   serves both the query-encode bound and #18's check, replacing a bare literal `6`.

### 13.2 #9 — a cut-off answer says so

**Verdict 4 = "answered, but cut off".** A new VALUE on the existing 1-byte verdict field: no wire
change, no version bump, no JRPL v3 and none of its 12-place lockstep.

- **PB reports why it stopped** over the existing `MSG_INFER_STATS` (0x11), which already arrives
  *before* the response chunks and is therefore latched in the same drain pass with no terminator
  race. `stop_reason` is a new byte on an **internal IPC struct, not a wire format** — PA and PB
  ship together, so there is nothing to version. PA still size-checks defensively, and checks the v4
  prefix and the new field **separately**: the naive `len < sizeof st` guard would, the moment the
  struct grew, have rejected the whole message and taken the live tok/s figure down with it.
- **Only `CAP` and `KV-FULL` are truncation.** `MODEL-ENDED-TURN` is the model finishing, and most
  answers finish — marking those truncated would cry wolf until the marker meant nothing.
- **The latch is cleared before dispatch.** It is global and every lane writes it; the workload lane
  hits its 50-token cap constantly, so without the clear nearly every control-IN answer would have
  been marked truncated. Clearing to `UNKNOWN` also means a stats message that never arrives reports
  *not truncated* rather than a fabricated claim.
- **A locally-served answer is never truncated.** SYSFACTS/DECLINE are rendered by PA and ran no
  generation, so the verdict-4 test is guarded on `!served_locally`.
- **The text is delivered identically either way.** Verdict 4 is a marker ON a genuine answer, not
  an error state — it is complete as far as it goes, and withholding it would discard work already
  done.

**§2e correction, carried from the prompt and confirmed against source: PA-side transport truncation
is currently UNREACHABLE.** The cap was derived so the clamp cannot bind — 250 × 5.66 B/token (the
*worst* measured density) = 1415 ≤ 1426. So the only reachable truncation today is PB's token cap,
and the PA-side clamp is a **guard for a future cap raise, not the live path**. A corollary caught in
passing: do **not** infer truncation from `roff == CTRL_REPLY_TEXT_MAX` — an answer that exactly fits
is not truncated, and that off-by-one would ship a false claim to the operator.

### 13.3 Honest limits

- **A boundary answer is reported CAP even if the model was about to finish.** If generation ends
  exactly at the cap, the next token *might* have been the end-of-turn — we cannot know without
  generating it. "We stopped because of the cap" is the honest report, and it is the conservative
  direction only in the sense that it never hides a real truncation; it can over-report on a
  1-in-250 coincidence.
- **A truncated answer entering the recall corpus is already handled**, and not by this change:
  `g3_clean_answer_len` cuts to the last COMPLETE SENTENCE, so a fragment tail is suppressed at
  recall time regardless of verdict.
- **The #18 evidence is llama.cpp on the Main PC**, not our engine, which is ~3.7× slower on the
  same silicon. Only the ratios transfer.
- **Thin-answer risk**, flagged unprompted by a judge: open questions now get single sentences that
  *"under a normal rubric would score as thin"*. 16 of 30 real questions want one line — the other
  14 do not. Worth watching once this is deployed.

### 13.4 KVM gate (2026-07-28) — and the finding the two changes produced together

**G1 — the workload lane is untouched, proven by byte-comparison against a same-fixture control.**
A pre-change build was run first on the same image in `--snapshot` mode, then the changed build.
Both: `q=100 hits=71 infer=13 hb=11 shield=5 err=0` — the long-standing signature — and the 14
`[INFER]` lines are **byte-identical, md5 `1ed542bbea2e994108b1cadf683b8e8c` on both**. Zero
`[PB] gen` / `[CTRL-IN-PROBE]` / `FRONTLOAD` / `TRUNCATED` lines in the deploy config, zero
FATAL/MODEL-BAD/ANOMALY. **Re-established a second time against the FINAL source** after the G4
probe legs were added — those are `#if JARVIS_CONTROL_IN_PROBE`-gated, but "it is gated so it cannot
matter" is an assumption, and the same md5 came back.

**G2 — the instruction is in the prompt, asserted from the artifact.** `instr=10` on every
control-IN generation (`[PB] gen n=38 prompt=83 instr=10 cap=250 …`), the measured count matching
what the constant encodes, `n_prompt` 24–83 against `prompt_ids[256]`. 0 `FRONTLOAD SKIPPED`.

**G3 — front-loading works and the §2c ordering is safe.** Answers open with the answer
(*"A page fault is a crucial event in virtual memory management that occurs when…"*,
*"A mutex (mutual exclusion) is a synchronization primitive used to…"*). **0 occurrences of
`<|channel>` / `thought`** — and this was not a soft test: one leg carried a **296-byte recall
preamble** (`[CTRL-RECALL] hit=1 recall=1 len=296`) ahead of the instruction, which is exactly the
mid-clause-preamble hazard `f1fc84b` documented. The ordering holds.

**G4 — truncation is reported, both directions.** All five verdicts observed in one run:
`stop=CAP` → `[CTRL-IN-RESP] TRUNCATED (stop=CAP) -> verdict=4` → `[CTRL-IN-REPLY] verdict=4 len=82`
**with the partial answer still delivered**; and two naturally-ending answers → `verdict=0`, NOT 4.

**THE FINDING, and it is the interesting part: #18 largely REMOVED the condition #9 exists to
report.** `"what is a page fault?"` was measured at M2 as `n=250 stop=CAP` — it ran to the cap. With
the front-loading instruction the same question ends at **`n=38 stop=MODEL-ENDED-TURN`**. Answering
directly instead of preambling does not just move the substance inside the budget, it means the
budget is no longer reached. Consequences, both real:

- **The verdict-4 branch would have shipped UNEXERCISED.** No ordinary question in the probe reached
  the cap any more. That is precisely how `JARVIS_RING_PROBE`'s silent chunk-drop survived — a
  branch nothing runs is a branch nobody has checked — so a probe-gated `CAPPROBE` marker query was
  added that clamps the cap to `PB_GEN_CAPPROBE_TOKENS` (12). It changes **only the cap**: the model
  genuinely runs out of budget mid-answer, which is the real condition, not a faked stop reason.
- **#9 is now mostly a guard rather than a routine signal** — which is the right outcome, and does
  not make it optional: the cap still binds on genuinely long explanatory answers, and #14
  (multi-frame) remains the constraint if the cap is ever raised.

**G5 — host suites, counts re-derived from the run:** receiver **277**, console honesty **203**,
console logic **27**, console e2e **49**; 0 FAIL.

**One pre-existing artifact, reported rather than hidden:** the probe's FIRST leg logs
`[CTRL-IN-PROBE] FAIL expected accept (input round trip)`. It sends **seq 1** and, unlike every
later leg, does **not** call `control_replay_init` first — so it runs against the floor persisted in
the shared KVM fixture, which this run read as `[CTRL-FLOOR] resumed floor=1542 wc=7`. Replay
protection correctly dropping a seq-1 frame below a floor of 1542 is the feature working. Not caused
by this change (which touches no parse/auth/replay code), and the same landmine the 6-5/M4d
pre-flight documented: *a low probe seq reads as a failure while the system behaves perfectly.*
