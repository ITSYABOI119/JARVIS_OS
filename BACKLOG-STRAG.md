# BACKLOG — strategist working tracker

> **RECONSTRUCTED 2026-08-01.** The original was an untracked file at the repo root and was deleted —
> never tracked, so git could not recover it, and no copy existed in any scratchpad or the recycle bin.
> `HANDOFF-strag.md` was lost with it. Rebuilt from the session transcript.
>
> **CANONICAL LOCATION IS NOW OUTSIDE THE REPO** (`~/.jarvis/`), so a repo clean, `git stash`, or a
> stray delete cannot take it. Copy into the repo root only if a session needs it there.
>
> **What is faithful:** THE TRACK, and every finding section written 2026-07-28 → 08-01 (they were
> authored in-conversation).
> **What is LOST and only partially recoverable:** the pre-2026-07-28 detail sections — the original
> "Open / not on the track" wording, the "Blocked" table, the completed-detail write-ups for #3
> (efibootmgr) and #4 (re-key), "Findings that change how items get done", and the older scheduling
> notes. What appears below for those is summarised from context, **not verbatim**.

**Last updated:** 2026-08-01 (post-C/M4; + test-coverage audit A1–A10, Cowork session) · **Repo at:** `347d96a` (`origin/master`, CI green — run 30681908759)
**Box:** Ubuntu · **ESP `0b771bba80831cdddef39afd9447c57a`** (SEMANTIC RECALL LIVE, `JARVIS_EMBED`=1, deployed 2026-08-01, boot_id 48) ·
**rollback `93a549bc82dabfb09a26eae2c4e7afff`** — ESP `.bak-pre-embed` + `~/jarvis_image.bak-pre-embed`, both md5-verified ·
kernel `d22affe8` · BootOrder `0001,0000` · no BootNext · **nothing owed**

**Tally:** 25 ✅ done · 3 ❌ decided against · 15 ⬜ open (incl. the NEW veto-build decision + the A1–A10 test-coverage audit) · 2 ⛔ blocked

---

## THE TRACK

**Everything on the previous track shipped.** Phase C's first lane — semantic recall — is **deployed
and live** (`JARVIS_EMBED`=1, boot 48, image `0b771bba…`). What follows is what is actually left.

| | do | est | why here |
|---|---|---|---|
| **N1** ✅ | **C/M4 routing measurement — DONE `347d96a` (2026-08-01), prompt consumed + deleted.** Verdict from the pre-registered bands: **arm C (hybrid veto) = WORTH BUILDING** (FP 32→6, FN 1, HELDOUT 70/73 = 95.89% unchanged); arm B (straight swap) = CLOSED at 90.41%. Both qualifications recorded in `CLAUDE.md` + `cm4_routing_results.json`: the veto fired on ZERO HELDOUT cases (no-harm evidence, NOT validation) and 55/69 corpus questions were author-written (the 14 verbatim-recovered ones cut 7→1, defusing the bias concern). **Building is a SEPARATE, still-open decision — see the decision row below** | — | closed |
| **DECIDE** | **Build the C/M4 veto, or park it?** The measurement says the mechanism works; the BUILD carries design costs the measurement deliberately did not price: every keyword-captured query pays an embed (~0.3–0.8 s idle, up to ~46 s mid-generation) — i.e. genuine status questions lose their instant answer; the veto needs PB, so it must SKIP (fall back to keyword) when PB is down/busy/times out to preserve the box-proven SYSFACTS-while-degraded property; the 3 DEV centroids need a committed on-box artifact (the `embed_mu.h` precedent). Any latency-confining variant (e.g. veto only no-self-reference captures) changes the measured configuration and must be RE-MEASURED off-box against the same bands first | operator call | the fork after N1 |
| **N2** | **#7 `poll_max` wall-time** | ~½ d | Still a comment, never measured. Gates any generation-cap raise, and a control-IN timeout feeds `KM2B_LANE_CTRL` where **three restart PB** — so a slow generation is still indistinguishable from a wedged PB, and the M2 cap raise (~46 s worst-case generation) NARROWED an unmeasured margin. The fix is a **liveness tick**, not a bigger timeout |
| **N3** | **#8 HUD `[UNUSABLE]` proof** | ~½ d | Needs `JARVIS_MODEL_FAIL_PROBE=1` — a **different image**, so it cannot ride a normal deploy. Its own induced-failure boot |
| **N4** | **#11 menu v2** | 1–2 d | Main-PC only, no box. Remaining: deploy / boot-into-JARVIS / clear-log / build+KVM smoke / semantic-store parser |
| **N5** | **#14 multi-frame replies** | several d | **Not binding today** — at the 250-token cap the worst density gives 1415 B ≤ 1426. Becomes binding only as part of raising the cap |

**NEW 2026-08-01:** a test-coverage audit added items **A1–A10** (own section below, ordered fastest →
longest). None block the track; A1–A5 are ≤1 h each and CI-only.

### Two riders that keep costing and are both cheap

| | |
|---|---|
| **59 stashes on the box clone** | The sync command begins `git stash`; `stash@{0}` shifts under a 59-deep pile. **Some may hold real work** — needs inspection, never `stash clear` |
| **Briefing worktree — 9th recurrence** | It is **debris, not a race**: lock mtimes match the last commit, 55 zombie `git.exe` all zero-CPU. Decisive test: **lock mtime vs `git log -1 --format=%ci`**. It has already cost a data-loss near-miss and a commit that pushed the wrong thing |

### Blocked / not raised

`4b route.c` bare-word SYSFACTS capture — **live on the box**, no keyword fix exists; **a measured embedder-veto fix now exists (`347d96a`, WORTH BUILDING) — building it is the open DECIDE row** ·
third-host wired capture (a cable) · **#15 6-7: ⛔ DO NOT RAISE** (`memory/no-soak-until-asked.md`) ·
#17 model swap — deprioritised, a one-sentence prompt change beat it.

---

## Shipped in this arc — 24 items, `1258e14` → `b566dbc`

Bench evidence preserved + generator keys committed · **front-loading shipped and deployed** (boot 47)
· bounded-elaboration measured and **rejected** · the whole embed arc: `MSG_EMBED` transport → vector
store → `mu` bit-exact on the box → selector at a **measured** floor → wiring → gates → v13 telemetry
→ **the flip** (boot 48) → recall provenance on the turn.

**The numbers that define the deployed feature:** 128 dims · mean-projection · floor **0.55** ·
**19/36 ≈ 53%** of paraphrases recall · **0 false recalls observed** (upper bound ~8%, never "zero") ·
**323–573 ms** and only on the fall-through path · a miss degrades to **exactly today's no-preamble
path**.

## T4b — the decision, stated once

**Ships:** on a control-IN question that is not a verbatim repeat, semantic recall fires **~53% of the
time** (19/36 measured, box-confirmed equal to host).

| | |
|---|---|
| false recalls | **0 observed on 36** — 95% upper bound ~8%. Never "zero". |
| cost | **323–573 ms**, and only on the fall-through path; exact-key hits pay nothing |
| miss behaviour | **exactly today's no-preamble path** — the failure mode is the status quo |
| false-recall blast radius | a degraded answer. K-b holds: recall can never mint an action |
| unproven until the flip | **on-wire v13** — KVM has no NIC |

**Caveat that must travel:** 19/36 is measured on **clean eval strings**. Traffic carries punctuation and
one `?` measured **0.038** of cosine. The two deployed paths were verified to preprocess identically, so
this is a **re-measurement** question, not an asymmetry — and normalising before embedding would
invalidate a floor measured on un-normalised strings.

---

## Open — not on the track *(summarised, not verbatim)*

| # | item | note |
|---|---|---|
| 7 | `poll_max` wall-time | never measured; gates any cap raise. A control-IN timeout feeds `KM2B_LANE_CTRL` and **three restart PB**, so the fix is a PB **liveness tick**, not a bigger timeout |
| 8 | HUD `[UNUSABLE]` proof | needs an induced failure; **cannot ride a deploy boot** — it requires `JARVIS_MODEL_FAIL_PROBE=1`, a different image |
| 11 | Menu v2 | Main-PC only. First slice shipped (`jarvis_admin.ps1`). Remaining: deploy / boot-into-JARVIS / clear-log / build+KVM smoke / semantic-store parser |
| 14 | Multi-frame replies | not binding at the 250-token cap; becomes binding only as part of raising it |
| 17 | Model swap (Bonsai low-bit) | **deprioritised** — a one-sentence prompt change beat it |
| 15 | 6-7 seven-day exit | **⛔ DO NOT RAISE.** Standing instruction, restated 2026-07-30. No position in the order, do not offer to write its prompt. `memory/no-soak-until-asked.md` |

## Test-coverage audit — A1–A10 *(NEW 2026-08-01, Cowork session; ordered fastest → longest)*

Audited the active tree (phase3/src + scripts, phase4/console, phasec) against `ci.yml` read in full
(103 steps). **Every claim below was measured with `diff`/`grep`/`cmp` on the working tree at audit
time, not recalled.** The module tier is strong — ~445 C test functions, `net/` at 1.85 test:src LOC,
four ASan/UBSan fuzzers, TSan double-builds, fixture drift gates, both-ways flag rebuilds. The gaps are
CONCENTRATED, not diffuse: **`sel4/` is 11,861 src LOC with ZERO test lines (~29% of phase3 source,
0% of its tests) — all 103 CI steps stop at the module boundary** — and two duplicated modules are
tested only in a stale copy. Full report delivered in the Cowork session
(`TEST_COVERAGE_ANALYSIS_2026-08-01.md`, not in-repo); this section is self-contained.
*(Also noted, no action urged: the phase1 Python agent suite never runs in CI — if that is intentional
freezing, one line in CLAUDE.md saying so stops it reading as a gap later.)*

| | do | est | why here |
|---|---|---|---|
| **A1** | **IPC drift gate** — one CI step `cmp`-ing the duplicated ring-buffer files (`dual_ring_buffer.c` + `ipc_handler.c` phase2↔phase3, `ring_buffer.c` phase1↔phase3) | ~¼ h | Byte-identical TODAY (verified), but CI compiles the phase1/phase2 copies — edit a phase3 copy and CI stays green while testing the old file. Turns "they happen to be identical" into an invariant — a grep, not a promise |
| **A2** | **Automate the two-sided `of=`/`seek=` invariant** in CI: menu `.ps1`+`.bat` → NOTHING (with `if=`/`skip=` as the positive control) · `jarvis_admin.ps1` → MATCHES · `jarvis_admin.bat` → NOTHING | ~¼ h | The menu's load-bearing safety property is verified today only when a human remembers to run the grep. ci.yml is a safe third home for the recipe — the greps target only the phasec files, so the self-match trap that keeps the recipe out of the scripts does not apply |
| **A3** | **Extend shellcheck**: add `build_jarvis_x86.sh` (58 KB — it builds the shipped image) + `create_boot_usb.sh` (destructive); rest of the bench/qemu scripts at `-S error` | ~½ h + findings | Only 4 of 16 `phase3/scripts/*.sh` are checked. Blowup risk stated: the first run on a 1,500-line script will surface a findings list — the "small items blow their estimates" pattern |
| **A4** | **`pwsh` parse + PSScriptAnalyzer** on ubuntu (pwsh preinstalled) over the 2,200 PS1 lines | ~1 h | Compatible with the documented "CI: N/A by design" stance — this PARSES, never runs; `-Check` stays the substitute for execution. Honest ceiling: catches syntax rot + common footguns; would NOT have caught the `$Lines` case-collision. A floor, not a replacement |
| **A5** | **CI completeness gate** — every `test_*.c`/`fuzz_*.c` under phase3/src must be referenced in ci.yml OR listed in an allowlist-with-reason; disposition the 2 current orphans (`test_gemma4_native.c`, `test_ggml_integration.c` — likely model-gated) | ~1 h | Makes CLAUDE.md's "ALWAYS add a CI step for new test files" rule mechanical instead of discipline. The same script inverted emits the module-with-no-test nag that surfaced this audit's findings |
| **A6** | **Python tooling closures**: `parse_nvme_log.py` round-trip (teach `test_nvme_log.c` `--dump`, reuse the `parse_episodic`/`parse_action_audit` fixture pattern) + a `gen_control_vectors.py --check` drift gate (the `gen_recall_pairs --check` precedent) | ~½ d | The only store parser of the three without a round-trip; and the vector generator feeds `fuzz_control_in` — if it drifts, the fuzzer weakens with no red light anywhere |
| **A7** | **Port the cache suite to the phase3 copies** (or retarget CI + dedupe): `test_cache.c` attests phase1's copies, while phase3's `cache_patterns.c` — which links into the shipped image via `main_x86.c` — has diverged **~880 diff lines** with ZERO tests of its own; `decision_cache.c` diverged 30 lines with LRU-only direct coverage | ~½ d, may blow | CI green here is misleading today. Porting may surface real behavioural drift between the copies — that is the point, and the reason the estimate can blow |
| **A8** | **Deepen host verification**: one CI job running the parser-facing unit suites (gguf, tokenizer, net_stack, `control_*`) under ASan/UBSan, + a nightly gcov/lcov artifact | ~1 d | Today only the fuzzers get sanitizers; the unit suites run plain `-O2`, and there is no coverage measurement anywhere (0 gcov/lcov mentions repo-wide). The lcov deliverable is branch coverage on `query_shield`/`route`/`control_verify` to feed fuzz corpora — not a vanity % |
| **A9** | **sel4-tier host-shim extraction slices** — continue the `km2b_miss`/`km2b_trigger`/`wake`/`control_floor` precedent into `main_x86.c` (8,835 lines / 292 static fns / 0 tests): the `ctrl_epi_write` index predicate (a DEBT CLAUDE.md already records with exactly this fix named), `pa_ctrl_gate` exit sequencing, the respawn sequencing | 1–2 d /slice | The respawn machinery is LIVE and box-proven, but has zero regression protection for future edits — and Phase C keeps editing near it (G4 respawn-mid-embed). One slice per change, the established pattern; not a rewrite. Highest-value testing work in the tree |
| **A10** | **QEMU boot-to-marker smoke in CI** — build the image (cache the seL4 kernel), boot a model-less sparse ≥10.1 GiB NVMe image to the `g_model_bad` fail-closed path + a serial marker | several d | `grep -ci qemu ci.yml` = 0: nothing in CI ever boots what ships; the whole wiring/link/boot class is invisible to all 103 steps. The MODEL_FAIL_PROBE mode-1 technique is already documented as the ~2-min no-model KVM boot, and the menu deferred "build + KVM smoke" for streaming/cancel reasons CI does not have (batch + timeout) |

## Blocked

| # | item | on |
|---|---|---|
| 4b | `route.c` bare-word SYSFACTS capture — **LIVE on the box** | Phase C. **No keyword fix exists** (`5e9d746`); the attempt was net worse than the defect. **C/M4 (`347d96a`) measured an embedder-veto fix at WORTH BUILDING** — the defect stays live until the build decision is taken |
| — | Third-host wired capture | a cable + capture point. The one 6-5 claim still NOT PROVEN |

## Decided against — closed on evidence

| # | item | why |
|---|---|---|
| 6b | `JARVIS_THINKING` flip | both pre-registered questions answered correctly with thinking OFF, nothing usable with it ON |
| 9b | thinking toggle in the chat UI | a toggle for a mode measured not worth having |
| 18b | bounded elaboration clause | **all three one-line questions gained a sentence.** Reopens only with CONDITIONALITY (route.c-shaped), never by rewording |

---

## Riders

| rider | note |
|---|---|
| **🔴 59 stashes on the box clone** | measured with `wc -l`; I first reported "3" from `\| head -3`. The sync command begins `git stash`. **Some may hold real work** — needs inspection, not `stash clear` |
| **🔴 Briefing worktree** | **10th recurrence (2026-08-01, during C/M4):** BOTH `HEAD.lock` AND a non-zero `index.lock` this time. The coder ran the decisive diagnostic (lock mtime matched the creating commit `e2028c2` to the second; 56 `git.exe`, newest 2h older than the locks), cleared, `git fsck` clean, verified both `e2028c2` and `b566dbc` survived as ancestors. Still DEBRIS, not a race. Decisive test: **lock mtime vs `git log -1 --format=%ci`** |
| **Protect the strategist working files** | **NEW 2026-08-01 — this file was deleted and unrecoverable.** Canonical copy now lives outside the repo. The old rider worried about accidental *commit*; the realised risk was accidental *deletion* |
| `.bat` exit-code masking | `pause` resets `ERRORLEVEL`; fixed in `jarvis_admin.bat`, still broken in `jarvis_menu.bat` / `start_receiver.bat` |
| Llama contamination verdict | vacated, not resolved — a template fix cannot cure memorised benchmarks |
| Suite-coverage rule | a green suite proves the rule under test only if POSITIVES exist in the quadrant the rule decides |

---

## Findings worth keeping

**`EPI_RESP_MAX 256` shapes recall.** The store holds a *head*. Consequences: it cannot be used to judge
answer completeness (I read a mid-clause ending as generation truncation and was wrong); it explains
`recall=3` not 4; and **front-loading makes answers recallable** because a short complete answer is
stored whole.

**The 0.55 floor's provenance.** `cm2_floor.py:59` renormalises `mu` before projecting, so the stored
bits are **not** the measurement's input. Double accumulation recovers them (0/1024 move); numpy's
float32 did not (1024/1024, 1 ULP). Immaterial (~1e-7) but **"bit-exact" is true of the artifact, not of
the measurement's inputs.**

**My margin rule was the wrong statistic.** Margin asks whether one threshold separates two populations;
topk only needs the winner to be right. Mean-projection is **load-bearing for safety** (raw = 12.5–28.6%
false), and the config with the *worse* margin has the *better* behaviour.

**Vacuous checks — five instances, one structural fix.** `objcopy -O binary` extracting nothing; a grep
matching a different function; mutants failing on a missing include; an unconditional `PASS:` line; a G4
that let PB publish before the respawn. **The fix: a gate that cannot achieve its precondition reports
INCONCLUSIVE, never PASS.** Plus: print the mutant diff, and run an unmutated control first.

**A check must not match its own documentation.** A `'tried - hits'` pattern hit the comment explaining
why that derivation is wrong — satisfiable only by deleting the documentation.

**Small items blow their estimates.** "Raise a cap" found six linked ceilings and a corrupting stack
array; "swap the selector" touched the store read, the vector compare, the preamble builder and three
incident-encoded hygiene rules.

**CI green can attest the wrong copy** *(from the 2026-08-01 audit)*. `test_cache.c` compiles phase1's
cache sources; the phase3 `cache_patterns.c` that links into the shipped image has diverged ~880 diff
lines with no test of its own, and the byte-identical IPC files are tested only at their phase1/phase2
paths. A duplicated module means the suite proves *a* file, not necessarily *the* file that ships. Where
copies must stay identical, `cmp` in CI (A1); where they legitimately may not, the suite moves to the
shipping copy (A7).
