# BLIND_SET.md — label → system key (front-load-only round, 2026-07-28)

This is the key for the **current** `BLIND_SET.md` in this directory: the four-arm
front-load-only comparison (Gemma 4 E2B and Llama 3.1 8B, each baseline vs front-loaded).

It is a **separate file on purpose.** `BLIND_SET.md` is a blind judging artifact and stays blind, so
it can be re-judged by a panel that has not seen which system is which. Do not merge this key into
its header.

**This key says which system produced which text. It does not re-validate any score** — see §6.

---

## ⚠️ 0. READ FIRST — labels do NOT survive across rounds

**Within one blind set, labels are fixed per system.** The set's own header says so and it holds.

**Across rounds they are not.** Each round's generator assigned labels independently. The same
`SYS-n` string means **different systems in different rounds**:

| label | this round (front-loading) | the `4274ca4` re-bench round |
|---|---|---|
| SYS-1 | Gemma 4 E2B — baseline | Gemma 4 E2B — greedy *(same by coincidence)* |
| **SYS-2** | **Gemma 4 E2B — front-loaded** | **Gemma 4 E4B** |
| SYS-3 | Llama 3.1 8B — baseline | Llama 3.1 8B — greedy *(same by coincidence)* |
| **SYS-4** | **Llama 3.1 8B — front-loaded** | **Gemma 4 E2B — recommended sampler** |

**The trap is worse than it looks: two of the four labels DO agree, and two do not.** Anyone
spot-checking SYS-1 or SYS-3, finding them consistent, and concluding the mapping carries over will
then misattribute **E4B's answers to E2B**. Partial agreement is more misleading than none.

> **Never carry a label mapping from one blind set to another. Read the key for the round in front
> of you.**

Which key belongs to which round:

| round | key file |
|---|---|
| 4-model re-bench (*"the incumbent is third"*) | `blind_key_rebench_4model.json` |
| front-loading, **two-clause** (`…, then elaborate.`) | `blind_key_frontload_twoclause.json` |
| front-loading, **front-load-only** — **this file's round** | `blind_key_frontload_only.json` |

---

## 1. The mapping — and the evidence is NOT the same strength for all four rows

| label | system | arm | evidence class |
|---|---|---|---|
| **SYS-1** | Gemma 4 E2B | baseline (no prefix) | **PROVEN** — byte-exact |
| **SYS-2** | Gemma 4 E2B | front-load-only | **RECORDED** — generator's own key, independently validated |
| **SYS-3** | Llama 3.1 8B Instruct | baseline (no prefix) | **PROVEN** — byte-exact |
| **SYS-4** | Llama 3.1 8B Instruct | front-load-only | **RECORDED** — generator's own key, independently validated |

**RECORDED is not the same class as PROVEN, and this file does not pretend it is.** PROVEN means a
byte-exact match against a committed artifact. RECORDED means the generator's own key file says so
and that key is independently validated — see §3. It is stronger than an inference and weaker than
a byte-exact match.

**Gemma 4 E4B is NOT in this run.** The committed E4B outputs in this directory belong to the earlier
model bench-off and match no SYS label here. This round was a four-arm E2B-vs-Llama comparison.

---

## 2. How the PROVEN rows were proven

The Q1 answer text (*"explain paging in one line"*) for SYS-1 and SYS-3 was taken from
`BLIND_SET.md` and searched, as an exact substring, across all seven committed per-model `.txt`
files in this directory. Each matched **exactly one** file and no other:

- **SYS-1** → `gemma-4-E2B-it-Q4_K_M.greedy.txt`
  (`Paging is a memory management technique that divides both physical memory and logical memory
  into fixed-size blocks to allow efficient allocation and protection.` — in `BLIND_SET.md` this
  text is wrapped in `**…**`; the inner text is what matches.)
- **SYS-3** → `Meta-Llama-3.1-8B-Instruct-Q4_K_M.greedy.txt`
  (`Paging is a memory management technique where a large memory space is divided into smaller,
  fixed-size blocks called pages, allowing for efficient allocation and deallocation of memory.`)

This works because the front-load-only round **re-ran only the front-loaded arms**; the two baseline
arms were carried over unchanged, so their text is byte-identical to the committed baseline outputs.

**The check that makes this safe rather than circular:** the SYS-2 and SYS-4 probe strings were
searched across the same seven files and matched **ZERO** of them. Had either matched, the
"only the front-loaded arms were re-run" premise would have been false and this whole recovery
invalid. It did not. **That check still does work and is retained.**

---

## 3. How the RECORDED rows are established

The front-load-only round left no surviving per-model artifact for SYS-2/SYS-4 (§5), so they cannot
be proven byte-exact. They rest on the **generator's own key**, `blind_key_frontload_only.json`,
which is documentary rather than deduced.

**The label continuity is recorded, not inferred.** The two-clause key and the front-load-only key
differ in **exactly two lines**, both prose descriptions:

```
<     "B": "Gemma 4 E2B  — FRONT-LOADED           [SUBJECT, after]",
>     "B": "Gemma 4 E2B  — FRONT-ONLY (1 clause)   [SUBJECT, after]",
<     "D": "Llama 3.1 8B — FRONT-LOADED           [CONTROL, after]"
>     "D": "Llama 3.1 8B — FRONT-ONLY (1 clause)   [CONTROL, after]"
```

`label_to_system` is **identical**, and all **twelve** `per_prompt_order` entries are **identical**.
The generator reused the same assignment and the same per-question shuffle.

**Why this is more than self-reference:** that same key file's SYS-1 and SYS-3 rows are the two
proven byte-exact in §2. **A key that is verifiably correct on every row that can be checked
independently is trustworthy on the rows that cannot.** It is validated where validation is
possible, which is what separates RECORDED from a bare assertion.

---

## 4. Key provenance — recovered from an ephemeral scratchpad

All three keys were recovered from an OS temp scratchpad **that gets reaped**. They are committed
here for that reason: they are the blind-judging provenance for three published rounds, including the
re-bench whose ranking is a committed conclusion.

| committed as | original filename | mtime | md5 | keys which round |
|---|---|---|---|---|
| `blind_key_rebench_4model.json` | `BLIND_KEY.json` | Jul 27 23:25 | `0247fefaf5d7014441cec8997dd8baf6` | the 4-model re-bench |
| `blind_key_frontload_twoclause.json` | `BLIND_KEY2.json` | Jul 28 14:57 | `e0c40a106c601e871bf999df2cb15b3e` | two-clause front-loading |
| `blind_key_frontload_only.json` | `BLIND_KEY4.json` | Jul 28 16:05 | `60de3a2cd9bc62d73f70d4762b435fc7` | front-load-only *(this round)* |

A fourth file, `BLIND_KEY3.json` (Jul 28 15:43), was **byte-identical** to `BLIND_KEY4.json` — same
md5 — so only one copy was taken. Renaming costs no provenance: the original names, mtimes and md5s
are recorded above, and each destination was md5-verified against its source after copying.

---

## 5. What does NOT survive

**The front-load-only per-model `.txt` files are gone — they exist nowhere.**

The **responses** survive in full in `BLIND_SET.md` (4 systems × 12 questions). What is lost is the
per-model artifacts: the per-answer `[speed]` tok/s lines, the harness header recording
`template_applied` / `think_leak`, and the built-prompt block.

Cause: `bench_models.sh` built its output path with `${PREFIX:+.frontload}`, which collapses **every**
non-empty prefix to one filename, so the front-load-only run overwrote the two-clause run in place.
That path now **fails closed** — a prefixed run must set `QUALITY_PREFIX_TAG` or the script aborts.

The two-clause per-model outputs were never at risk; they are committed and are the `.frontload.txt`
files in this directory.

---

## 6. Scoring caveat — read this before comparing any numbers

The judging rubric for the front-load-only round gained a **fifth rule** that earlier rounds did not
have: *"a short complete answer beats a longer one that is cut off."*

Consequently:

- **Within-run paired deltas are valid.** Both arms of this run were judged under the same rubric and
  produced by the same harness.
- **Cross-run absolute comparison is NOT valid.** This round's baseline scores are not comparable to
  the earlier rounds' baselines or to the published bench-off figures; those reflect different
  (rubric, harness) combinations.

**This key does not re-validate any score.** It establishes authorship of text, nothing more.

---

## 7. What each arm was given

| arm | prefix inside the user turn |
|---|---|
| SYS-1, SYS-3 (baselines) | **none** |
| SYS-2, SYS-4 (front-loaded) | `Answer the question directly in your first sentence.` |

Single clause. This is **not** the earlier two-clause instruction (`…, then elaborate.`), which is the
one that drove length violations up and is recorded separately in
`phase6/docs/MODEL_BENCH_2026-07.md` §8.

Both arms otherwise identical: cap 250, greedy, `--jinja`, `-rea off`, same 12 questions.
