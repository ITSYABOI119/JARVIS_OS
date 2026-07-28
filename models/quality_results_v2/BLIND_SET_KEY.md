# BLIND_SET.md — label → system key (front-load-only round, 2026-07-28)

This is the key for the **current** `BLIND_SET.md` in this directory: the four-arm
front-load-only comparison (Gemma 4 E2B and Llama 3.1 8B, each baseline vs front-loaded).

It is a **separate file on purpose.** `BLIND_SET.md` is a blind judging artifact and stays blind, so
it can be re-judged by a panel that has not seen which system is which. Do not merge this key into
its header.

**This key says which system produced which text. It does not re-validate any score** — see §5.

---

## 1. The mapping — and the evidence is NOT the same strength for all four rows

| label | system | arm | evidence class |
|---|---|---|---|
| **SYS-1** | Gemma 4 E2B | baseline (no prefix) | **PROVEN** — byte-exact |
| **SYS-2** | Gemma 4 E2B | front-load-only | **INFERRED** — label continuity |
| **SYS-3** | Llama 3.1 8B Instruct | baseline (no prefix) | **PROVEN** — byte-exact |
| **SYS-4** | Llama 3.1 8B Instruct | front-load-only | **INFERRED** — label continuity |

**Two rows are proven and two are inferred. Do not quote this table without that distinction.**

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
invalid. It did not.

---

## 3. How the INFERRED rows were inferred

There is no surviving per-model artifact from the front-load-only round to match against (§4), so
SYS-2 and SYS-4 rest on three converging arguments rather than a byte-exact match:

1. **Label continuity by construction.** The anonymiser assigns labels deterministically from the
   sorted system keys, so the label order is identical between the two blind sets built by it.
2. **`BLIND_SET.md`'s own header** asserts that each label is FIXED to one system for all 12
   questions, and only the presentation order shuffles. (That property was itself a correction — an
   earlier version of the anonymiser assigned labels by *position*, which a judge caught.)
3. **Cross-set confirmation.** In the git-`HEAD` copy of `BLIND_SET.md` — the earlier *two-clause*
   round — SYS-2's Q1 answer matches `gemma-4-E2B-it-Q4_K_M.greedy.frontload.txt` byte-exact. That
   fixes SYS-2 = "E2B, front-loaded" in the set that still has its artifacts, and the label ordering
   is unchanged between the two sets.

**Corroboration, recorded but not counted as proof:** the run's own generated key file existed in
session scratch (`BLIND_KEY4.json`, not committed) and agreed with the mapping above on all four
rows. It is not a durable repo artifact and shares provenance with the thing it corroborates, so it
does not upgrade SYS-2/SYS-4 to PROVEN.

---

## 4. What does NOT survive

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

## 5. Scoring caveat — read this before comparing any numbers

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

## 6. What each arm was given

| arm | prefix inside the user turn |
|---|---|
| SYS-1, SYS-3 (baselines) | **none** |
| SYS-2, SYS-4 (front-loaded) | `Answer the question directly in your first sentence.` |

Single clause. This is **not** the earlier two-clause instruction (`…, then elaborate.`), which is the
one that drove length violations up and is recorded separately in
`phase6/docs/MODEL_BENCH_2026-07.md` §8.

Both arms otherwise identical: cap 250, greedy, `--jinja`, `-rea off`, same 12 questions.
