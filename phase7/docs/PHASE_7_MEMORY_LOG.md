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

---

## MS0.1 — 2026-09-07 — the predicate-aware full-text lane; the growth band re-measured against its negative control

**The disposition.** MS0 met four of five bands and missed `growth_drop<=5` at 46.25 points, with the cause
measured rather than argued: 30× filler about disjoint people and places put `works as` and `lives in` into
roughly half of all rows, collapsing those terms' IDF to ≈ 0.02, after which BM25's length normalisation
returned the SHORTER of two facts about the same person. The strategist's disposition, now in the design
(§6), is the K-b instinct one level out: the registry carries a static, human-reviewed QUERY VOCABULARY per
predicate, and a question matching **exactly one** predicate's vocabulary restricts the fact and preference
lookups to that predicate; none or several leaves the lookup open. The rule SELECTS a predicate the store
already types and can never invent one. The same slice puts preference rows into the index for the first
time — MS0's transfer number was 0 for a structural reason before the vocabulary one.

### The vocabulary (`registry.QUERY_VOCAB`, nine pairwise-disjoint sets, matched on raw lower-cased tokens)

| predicate | words |
|---|---|
| `person.name` | name, named, called, call |
| `person.relation_to` | wife, husband, married, marry, spouse, partner, related, relationship, relation, sister, brother, mother, father, mum, dad, son, daughter, friend, friends, colleague, family |
| `person.lives_in` | live, lives, living, lived, home, address, reside, resides, residing |
| `person.works_as` | work, works, working, worked, job, jobs, occupation, employed, employer, career, profession |
| `person.habit` | habit, habits, routine, routines, usually, regularly |
| `person.trait` | trait, traits, personality, tends, tend, tendency |
| `household.topic` | talk, talks, talked, talking, discuss, discussed, discussing, topic, topics, conversation, conversations |
| `household.routine` | schedule, schedules, household, weekly, chores, chore |
| `owner.prefers` | like, likes, liked, love, loves, loved, prefer, prefers, preferred, favourite, favorite, hate, hates, hated, dislike, dislikes, avoid, avoids, enjoy, enjoys, want, wants |

Inflections are spelled out because matching is on raw tokens, never stemmed. Disjointness and key-validity
are asserted by the suite (T18l, T18m), so a word added to two sets fails the build rather than quietly
making the hint ambiguous.

### Run 1 — THE NEGATIVE CONTROL (the hint off), Main PC, the voice venv, 14.5 s

```
python.exe phase7/memory/bench_ms0.py --households 10 --days 14 --seed 1 \
    --latency-facts 100000 --no-predicate-hint --out phase7/memory/bench/results/ms0_1_control_off.json
```

```
households : 10  seeds 1..10  days 14  predicate_hint OFF (negative control)
aggregate  :
    coexist_recall                   1.0
    growth_drop_points               46.25
    growth_update_acc                0.5
    relation_precision               1.0
    spouse_surfaced_day_mean         8.0
    spouse_surfaced_households       10/10
    transfer_recall5                 0.0
    update_acc                       0.9625
latency    : p50 0.0972 ms  p99 0.3468 ms over 100000 ingests, 100000 facts in the store
audit      : 0 violations
bands      :
    PASS audit==0
    PASS coexist_recall>=0.95
    FAIL growth_drop<=5
    PASS p99<=50ms
    PASS update_acc>=0.95
```

**This is the load-bearing result of the milestone.** Every band-relevant field reproduces MS0's recorded run
exactly — `update_acc 0.9625`, `coexist_recall 1.0`, `growth_update_acc 0.5`, `growth_drop_points 46.25`,
`transfer_recall5 0.0`, `relation_precision 1.0`, `spouse_surfaced_day_mean 8.0`, 0 audit violations — with
only latency free to differ (p50 0.0972 against MS0's 0.0985, p99 0.3468 against 0.354). Nothing but the hint
changed, so the ON numbers below measure the hint and nothing else.

### Run 2 — the predicate-aware lane (the hint on), the same command plus `--assert-bands`, 14.5 s

```
households : 10  seeds 1..10  days 14  predicate_hint ON
aggregate  :
    coexist_recall                   1.0
    growth_drop_points               0.0
    growth_update_acc                1.0
    relation_precision               1.0
    spouse_surfaced_day_mean         8.0
    spouse_surfaced_households       10/10
    transfer_recall5                 0.0
    update_acc                       1.0
latency    : p50 0.099 ms  p99 0.3558 ms over 100000 ingests, 100000 facts in the store
audit      : 0 violations
bands      :
    PASS audit==0
    PASS coexist_recall>=0.95
    PASS growth_drop<=5
    PASS p99<=50ms
    PASS update_acc>=0.95
```

`--assert-bands` exited 0. **All five bands are met.**

| band (design §8, MS0 column) | hint OFF | hint ON | verdict |
|---|---|---|---|
| update accuracy ≥ 95 % | 96.25 % | **100 %** | PASS |
| coexisting recall ≥ 95 % | 100 % | **100 %** | PASS |
| audit completeness 100 % | 0 violations | **0 violations** | PASS |
| write p99 ≤ 50 ms at 100 k | 0.3468 ms | **0.3558 ms** | PASS |
| growth drop ≤ 5 points | **46.25** (MISS) | **0.0** | PASS |
| transfer recall@5 | 0.0 | 0.0 | reported, not banded |
| relation precision at ≥ 0.80 | 1.0 | 1.0 | reported (oracle) |

### Per household, both runs

```
seed |  OFF update  drop  |  ON update  drop  | spouse day OFF/ON | ended leaks
  1  |    1.0      50.0   |    1.0      0.0   |       8/8         |  0/0
  2  |    0.875    37.5   |    1.0      0.0   |       8/8         |  0/0
  3  |    1.0      50.0   |    1.0      0.0   |       8/8         |  0/0
  4  |    1.0      50.0   |    1.0      0.0   |       8/8         |  0/0
  5  |    0.875    37.5   |    1.0      0.0   |       8/8         |  0/0
  6  |    1.0      50.0   |    1.0      0.0   |       8/8         |  0/0
  7  |    0.875    37.5   |    1.0      0.0   |       8/8         |  0/0
  8  |    1.0      50.0   |    1.0      0.0   |       8/8         |  0/0
  9  |    1.0      50.0   |    1.0      0.0   |       8/8         |  0/0
 10  |    1.0      50.0   |    1.0      0.0   |       8/8         |  0/0
```

The three seeds that lost an update question at MS0 (2, 5, 7 — the habit *"cycles to work"* answering a work
question) now answer all eight, and the write path is untouched: the spouse edge still surfaces on day 8 in
10/10 households either way, which is the check that the hint changed retrieval and nothing else.

### Tests

`phase7/memory/test_memory_logic.py` — **144 checks, all passing** in WSL Python 3.12.3 / SQLite 3.45.1 (the
CI form), the voice venv's 3.12.6 / 3.45.3, and the GitHub runner. T18 covers the hint and the vocabulary
(including `does alex live near where he works` → None, two predicates, and the disjointness assertion),
T19 the MS0 F2 collision resolved in the store, T20 preferences through index / close / purge, T21 the
negative control in-process.

One mutant, control first (144/144), applied to a throwaway copy outside the repo: **`predicate_hint`
returning `None` always** →

```
FAIL T18 hint('where does alex live') -> person.lives_in got None      (and seven more T18 rows)
FAIL T21c hint ON lifts seed-2 update accuracy to 1.0   0.875
FAIL T21d hint ON removes the growth drop entirely      37.5
134/144 checks passed
```

The mutant's T21 values fall back to exactly MS0's seed-2 figures, which is the sharpest available evidence
that the hint is the whole mechanism and that nothing else moved.

### A defect fixed on the way, latent since MS0

Purging a cluster whose preference had already been ENDED raised `sqlite3.DatabaseError: database disk image
is malformed`. FTS5's `'delete'` command on a contentless table does not tolerate being asked twice: the row
had left the index at close time, and deleting an absent rowid corrupts the index outright. The same hazard
was **latent for facts since MS0** — no test had yet purged a *superseded* fact. Both paths now go through
`_drop_from_index`, which deletes only while `valid_to is null`, and T20i purges a superseded fact and reads
the index back.

### Honest scope

The hint is a **rule over a fixed vocabulary**, measured on a **template corpus with fixed query shapes**. It
is the deterministic baseline of the design's "route by fact type"; the general mechanism is the embedding
lane at MS1. A real question whose words fall outside the vocabulary gets exactly the MS0 behaviour —
unrestricted — which is why the rule is "exactly one match" rather than "best guess": a wrong restriction
hides the answer completely, while no restriction only leaves MS0 in place.

Three corpus queries hint other than their own predicate, and none was patched by adding a word (the table is
human-reviewed, not a tuning knob):

- `what household routine do we keep` → **None**, because *household* is in `household.routine` while
  *routine* is in `person.habit`. It falls back to the MS0 behaviour and still scores 1.0, but the most
  natural household-routine question straddles two sets.
- `when should we schedule the appointment` → **`household.routine`** and `booking a flight time that works`
  → **`person.works_as`**; both are transfer scenarios, so the hint restricts them away from the preference
  table they are meant to find. Neither changes the reported number (transfer is 0.0 regardless — the
  scenarios share no word with their preference), but they show the rule's real failure mode: a scenario
  query that happens to contain one vocabulary word is restricted to the wrong predicate.

`transfer_recall5` stays **0.0** and is still REPORTED, never banded. The structural zero is gone —
preferences are in the index now, and T20a proves a preference question reaches one — so the remaining zero
is purely the vocabulary gap the embedding lane exists for. `relation_precision` stays 1.0 **by
construction**: the oracle plants only true relations, so it says the store surfaced what it was given.
Nothing here is measured on real speech, on an extractor, or on the owner.
