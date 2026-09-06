# Phase 7 — Memory Store: the measurement log

Every measured run of the household memory store, newest milestone last. The spec is
`phase7/docs/PHASE_7_MEMORY_DESIGN.md`; the bands quoted here are its §8, pre-registered before the
code that measures them existed. A number in this file was produced by a command in this file, on
the machine named, and a band that is missed is recorded as a miss with its cause — never retuned.

---

## MS0 — 2026-09-06 — the store, the rules and the benchmark on oracle candidates

**What landed.** The pure decision core (`registry`, `candidate`, `confidence`, `freshness`,
`rules` R1–R4, `retrieve`, `people`), the SQLite store with FTS5 (`schema`, `store`) carrying ingest
through the rules in one transaction with its audit rows, R5 recompute from spans, the R7 purge
cascade, personhood, and the full-text query with the spans attached; the seeded template corpus and
the harness over the five §8 sets. Standard library only — no model, no GPU, no numpy, no audio.

### The recorded run

Main PC, `%USERPROFILE%\.jarvis\voice\venv\Scripts\python.exe` (Python 3.12.6, SQLite 3.45.3 with
FTS5), 14.3 s wall:

```
python3 phase7/memory/bench_ms0.py --households 10 --days 14 --seed 1 \
    --latency-facts 100000 --out phase7/memory/bench/results/ms0_run.json --assert-bands
```

```
households : 10  seeds 1..10  days 14
aggregate  :
    coexist_recall                   1.0
    growth_drop_points               46.25
    growth_update_acc                0.5
    relation_precision               1.0
    spouse_surfaced_day_mean         8.0
    spouse_surfaced_households       10/10
    transfer_recall5                 0.0
    update_acc                       0.9625
latency    : p50 0.0985 ms  p99 0.354 ms over 100000 ingests, 100000 facts in the store
audit      : 0 violations
bands      :
    PASS audit==0
    PASS coexist_recall>=0.95
    FAIL growth_drop<=5
    PASS p99<=50ms
    PASS update_acc>=0.95
reported   : {"relation_precision": 1.0, "spouse_surfaced_day_mean": 8.0, "transfer_recall5": 0.0}
```

`--assert-bands` exited 1 on the missed band. The full per-household detail is
`phase7/memory/bench/results/ms0_run.json` (5,828 bytes, synthetic).

**Four of five bands met. One missed, and it is the finding of this milestone.**

| band (design §8, MS0 column) | measured | verdict |
|---|---|---|
| update accuracy ≥ 95 % | **96.25 %** | PASS |
| coexisting recall ≥ 95 % | **100 %** | PASS |
| audit completeness 100 % | **0 violations** | PASS |
| write p99 ≤ 50 ms at 100 k facts | **0.354 ms** (p50 0.0985 ms) | PASS, by two orders of magnitude |
| growth drop ≤ 5 points | **46.25 points** | **MISS** |
| transfer recall@5 | 0.0 | reported, not banded (moved to MS1) |
| relation precision at ≥ 0.80 | 1.0 over 2 surfaced edges per household | reported, not banded |

### The guess behaved exactly as the design predicts

The owner→partner `spouse` edge is built only from INFERRED evidence and its confidence is
recomputed from spans every time. Across **10 of 10** households it crossed the 0.80 surfacing
threshold on **day 8** — the fifth distinct supporting day (1, 3, 5, 6, 8), `1 − e^(−5/3) = 0.8111` —
dipped to 0.7364 after the day-11 contradiction, and finished at **0.8647** on day 14 (7 supporting
days against 1 contradicting, `1 − e^(−6/3)`). Identical in every household, which is what a
deterministic rule over dated evidence should give. The partner→owner edge, which she states, sits
at 1.0 from its first day; the contrast between the two is the point.

The design's amended §3.4 is load-bearing here: the partner earns personhood on day 3, and the edge
counts her day-1 spans as well, so the count is five supporting days by day 8 rather than seven.

### The missed band, and its cause

`growth_drop_points` measures how much update accuracy falls once 30× unrelated transcript is added
(1,110 filler candidates per household against ~37 gold ones). It fell **46.25 points** — from
96.25 % to 50 % — against a ≤ 5 band.

The cause was measured, not guessed. After growth, all four *lives_in* questions still answer
correctly and all four *works_as* questions return the **`lives_in`** fact about the same person.
Reading the BM25 values directly for `what does alex do for work` in a grown store of 1,126 current
facts:

```
  bm25=  -5.7317  person.lives_in    Bendigo        <- returned
  bm25=  -5.3411  person.works_as    a plumber      <- wanted
  docs matching 'alex'  : 4
  docs matching 'work'  : 557
  docs matching 'live'  : 557
  docs matching 'person': 1118
```

(SQLite's `bm25()` is negative-better.) The filler is about disjoint people in disjoint places, so
it never competes on the rare term `alex` — but it does put `works as` and `lives in` into half the
documents each, collapsing those terms' IDF to ≈ 0.02. Once the predicate word stops discriminating,
the only thing separating two facts about the same person is BM25's document-length normalisation,
and `alex person lives in Bendigo` (5 tokens) beats `alex person works as a plumber` (6 tokens).

A diagnostic pins it: rerunning one household with the filler moved onto a predicate the update set
never asks about gives **growth_drop_points = 0.0** with update accuracy 1.0 before and after. So
the drop is entirely predicate-term dilution in the full-text lane, not index size, not the ranker,
and not a collision with the gold facts.

**Nothing was retuned.** The corpus, the rendering (`"<subject display name> <predicate words>
<object_text>"`), the ranker weights, `TAU_DAYS` and the surfacing threshold are all as the design
specifies. This is a pre-registered prediction that the measurement falsified, which is what the
benchmark is for. It is the same shape as the transfer band the design already moved to MS1: the
full-text lane alone cannot carry a question whose discriminating word is common. The disposition —
whether the band belongs at MS1 beside the embedding lane, or whether MS0 owes a fix — is the
strategist's.

One further honest number: update accuracy is 96.25 %, not 100 %, and the single failure is real.
In seed 2 the partner's habit is *"cycles to work"*, so `what does juno do for work` returns that
habit rather than her `works_as` fact. A word-level lane cannot tell the two apart.

### The CI-sized run

```
python3 phase7/memory/bench_ms0.py --households 3 --days 14 --seed 1 \
    --latency-facts 20000 --out /tmp/ms0_ci.json
```

4.3 s wall: `update_acc 0.9583 · coexist_recall 1.0 · growth_drop_points 45.8333 ·
relation_precision 1.0 · spouse_surfaced_day_mean 8.0 (3/3) · transfer_recall5 0.0 ·
latency p50 0.1272 ms p99 0.4905 ms over 20,000 ingests · 0 audit violations`. This is the shape
CI runs; `--assert-bands` is deliberately not passed there while `growth_drop<=5` stands as a
published miss, and the reason is written above the step in `ci.yml`.

### Tests

`phase7/memory/test_memory_logic.py` — **114 checks, all passing** in three venues: WSL Python
3.12.3 / SQLite 3.45.1 (the CI form), the voice venv's Python 3.12.6 / SQLite 3.45.3, and the
GitHub runner's Python 3.12.14. T1–T8 cover the pure core, T9–T16 the store on in-memory databases,
T17 the corpus.

Mutation evidence — the unmutated control ran first every time, and every mutant was applied to a
throwaway copy outside the repo:

| mutant | expected | result |
|---|---|---|
| A: T5b expectation `supersede` → `history` | fail by name | `FAIL T5b newer stated_owner -> supersede`, 66/67 |
| B: `rules.decide` lets a history candidate keep its closes | fail by name | `FAIL T5j a history candidate closes nothing…`, 66/67 |
| C: T10c expects the superseded row still current | fail by name | `FAIL T10c exactly one current row, the new one`, 101/102 |
| D: the superseded fact is left in the full-text index | fail by name | **survived at first — 102/102** |

Mutant D is worth recording rather than hiding. It survived because two independent mechanisms keep
a superseded fact out of an answer — the `valid_to is null` join filter *and* the FTS delete — and
the test only exercised the first. A check on the index itself (`T14c2`) was added, after which
mutant D fails by name (`FAIL T14c2 the superseded row is gone from the full-text index itself`).
The gap was in the test, not the store.

### Honest scope

MS0 measures **the store, not a memory system**. The candidates are ORACLE — written by the corpus
generator, not extracted from speech — so nothing here says anything about extraction quality; that
is MS1's bake-off and it is the binding constraint the research names. The retrieval is the
**full-text lane alone**; the embedding lane lands at MS1, which is why transfer recall is 0.0 and
is reported rather than banded. The corpus is a **seeded template**, not language-model output and
not a transcript: its sentences are short and regular, which flatters BM25 on the update set and
makes the growth result, if anything, generous. Relation precision is 1.0 **by construction** — the
oracle plants only true relations, so the number says the store surfaced what it was given and
nothing about whether a real extractor would propose a wrong one.

Nothing in this milestone touched the box, the Pi, a GPU, a recording, a transcript, or anything
about the owner or his household. No row here supports the words "knows", "understands" or
"remembers your life"; what MS0 shows is that a belief can be written, superseded, contradicted,
recalled with its evidence, and purged, with every loss auditable — and that at household scale the
write path costs about a tenth of a millisecond.
