# Phase 7: Autonomy — Plan

**Status:** ACTIVE (stood up 2026-09-05; Phase 6 CLOSED the same day by ADR `docs/decisions/2026-09-05-close-phase-6-defer-supervised-exit.md`). First arc: the operator's choice (§1).
**Prerequisite:** Phase 6 closed; the deployed image runs the 13-flag default-ON set; the pending-deploy delta is one commit (`3f676a2`).
**Estimated effort:** 12–18 months (canon).
**Sources:** `phase4/docs/ROADMAP.md` §Phase 7 (goals + done-when, canon — quoted verbatim below) + §Cross-phase backlog B3 + §Beyond Phase 7 + §"What each phase does *not* include"; `phase6/docs/PHASE_6_FINAL_REPORT.md` (the Phase 7 handoff); `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md`; `phase6/docs/SOAK_2026-08_FINAL_REPORT.md`; `phase6/docs/PHASE_6_GOAL_C_EMBEDDER.md`; `phase3/scripts/embed/README.md`; the CLAUDE.md rows and the `docs/CLAUDE_RECORD.md` entries named per section. ROADMAP line numbers are as of the commit that landed this plan — re-grep the quoted text before relying on one.

> **Mission (canon, `ROADMAP.md:109`):** JARVIS can run safely on its own for extended periods, retrieve memories associatively, and improve — within seL4 capability bounds.

---

## 0. Status board — where Phase 7 is up to (the live truth; update in the same commit that completes a row)

This board is the one place that says where Phase 7 is up to: one row per goal-arc-milestone, with the evidence — commit, date, boot or CI run — in the cell beside the status. The rule: a cell becomes `DONE`, followed by the commit hash and the date, in the commit that lands the work or in the flip commit immediately after it that cites the landing hash — never in a later session; the strategist's prompt names the row to flip; a cell never says done without a hash behind it. §0 is the first thing a session reads about Phase 7, before the strategy or the canon below.

| Goal | Arc / milestone | Status | Evidence (commit · date · boot/run) | Source |
|---|---|---|---|---|
| 7.1 Associative memory | Baseline: the deployed semantic-recall lane | `MEASURED BASELINE` | 19/36 = 53 % paraphrases at the 0.55 floor (C/M2b, boot 48, 2026-08-01); done-when ≥80 % | CLAUDE.md `JARVIS_EMBED` flag row and the "C/M2b — SEMANTIC RECALL WIRED" row; `ROADMAP.md:127` |
| 7.1 | Arc A: raise paraphrase recall to ≥80 % on the eval corpus (floor/dims/model measured first; the 2070 fine-tune only on a measured miss) | `NOT STARTED` | — | `phase3/scripts/embed/README.md:14-16`; `phase6/docs/PHASE_6_GOAL_C_EMBEDDER.md:24-26,101-107` |
| 7.2 30-day autonomous operation | The run | `OPERATOR-SCHEDULED` | unattended envelope 7 d 18 h 18 m, err=0 (2026-08 soak) | `phase6/docs/SOAK_2026-08_FINAL_REPORT.md:13-19`; `ROADMAP.md:126` |
| 7.3 Self-modification (staged) | Design: sandbox → static checks → staged deploy → atomic rollback over the K spine + the ESP dual-boot | `NOT STARTED` | seeds: K spine, JACT, ESP rollback images, boot-smoke | CLAUDE.md rows "Action Allowlist", "SHIELD Action Gate", "Action-Audit Store", "x86 Installer", "CI boots the shipped image (A10, 2026-08-09)" |
| 7.4 Larger models | GPU path | `BLOCKED (hardware; ADR 2026-06-16)` | — | `docs/decisions/2026-06-16-defer-gpu-inference.md`; CLAUDE.md §Hardware Setup |
| 7.5 Cross-session personality | Design over the control-IN + episodic stores | `NOT STARTED` | seeds: control-IN store @21,140,000, cross-session recall since 2026-07-22 | CLAUDE.md `JARVIS_CONTROL_IN_RECALL` flag row; "Semantic Memory (Phase 5 #4/M0)" row |
| 7.6 External security audit | Engagement | `NOT STARTED` | internal baseline: 41 fixed / 10 accepted-or-deferred / 0 open HIGH-MED | CLAUDE.md §Codebase Metrics, the Security bullet |
| 7.7 Release v2.0.0 | Tag | `NOT STARTED` | — | `ROADMAP.md:121,133` |
| 7.8 | M0a: the owner-voice tooling on the Main PC (record / enroll / verify / transcribe / evaluate) + the public-speaker self-test | `DONE 94fa0f7 2026-09-06` | EER 0.00 % / threshold 0.5179 on 28 positives + 78 negatives from 39 LibriSpeech speakers; ASR faster-whisper large-v3 float16 RTF 0.132; raw-audio deletion proven; CI step green | `phase7/docs/PHASE_7_GOAL_8_VOICE.md` §6; commit 94fa0f7 |
| 7.8 Ambient voice wearable (household voice learning; canon since 2026-09-05, operator decision, scope per idea doc §8) | Owner's voice first: enrolled from the Main PC headset mic; owner-versus-not speaker verification measured on held-out recordings at a pre-registered rate | `NOT STARTED` | — | `ROADMAP.md:122`; `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md` §8 |
| 7.8 | Transcribe-everything pipeline: Whisper (RTX 2070) → speaker clustering → raw audio deleted after transcription → one clean purge action per speaker cluster (non-household speech is the owner's to delete by hand) | `NOT STARTED` | — | idea doc §8 (supersedes §3) |
| 7.8 | Memory research first: what state-of-the-art agent memory does today, with pre-registered questions, then the design of ours — the substrate 7.1 (associative memory), 7.5 (personality) and 7.8 share; the owner will spend a whole phase on this one thing if needed | `DONE 33c69db 2026-09-06` | the survey `phase7/docs/PHASE_7_MEMORY_RESEARCH.md`: 24 sources, 120 claims, 25 voted (9 V / 12 R / 4 X; numbers held in 24 of 25, every R is framing); §8 nine implications, §8a what the votes changed; the design doc is next | idea doc §8; `ROADMAP.md:115` (goal 1), `:119` (goal 5) |
| 7.8 | The memory store (purpose-built, state of the art, Main PC) + the guess: with only the owner enrolled, JARVIS names the recurring second voice as the owner's wife and who she is to him, over days, with a confidence; inferred facts used without confirmation | `NOT STARTED` | — | idea doc §8 |
| 7.8 | Profile view: the household profile on the console, live from the store, designed clean in Claude Design — the UI–feature-parity rule applies, real source only | `NOT STARTED` | — | idea doc §8; CLAUDE.md row "Telemetry Console", the §Rules UI–feature-parity bullet, the §Technology Stack Claude Design bullet |
| 7.8 | Digest: new learning surfaced proactively — a learned-this-week digest on the console from the same store; the box's own digest may count distilled facts received | `NOT STARTED` | — | idea doc §8; CLAUDE.md row "Behavior Registry (Phase 6 6-3/M0, host)" (B4 status digest is the box-side precedent) |
| 7.8 | V0: a headset command → Whisper → the receiver-as-signer → control-IN (no new hardware) | `NOT STARTED` | — | idea doc §4; CLAUDE.md row "Telemetry Receiver / SSE bridge" |
| 7.8 | V1: the continuous recorder wearable — any small form (the bracelet was the first idea); ESP32-S3-class, I2S mic, microSD, USB-C mass-storage/charge, radios off; all-day capture, nightly plug-in → pipeline → wipe | `NOT STARTED` | — | idea doc §2, §4, §8 |
| 7.8 | V2: speaker-verified wake-word commands from the wearable → verify the owner's voice → control-IN | `NOT STARTED` | — | idea doc §4 |
| 7.8 | V3: wireless or live variants; opt-in sessions for guests | `BLOCKED (decided last, per idea doc §4 — only if still wanted after the pipeline proves value)` | — | idea doc §4, §8 |

### Carried backlog (not goals; each row names its source)

| Item | What it is today | Source |
|---|---|---|
| The undeployed delta | ONE image-compiled commit, `3f676a2` — the 2026-09-04 `-Wall` cleanup of `main_x86.c`, behaviour-neutral, KVM-gated, not on the box; whether the next run uses the deployed image or a rebuild carrying it is the operator's call | CLAUDE.md Current paragraph (PENDING-DEPLOY DELTA); `phase6/docs/PHASE_6_GOAL_6-7_SOAK.md:28,127` |
| Dead-linked ring modules | `src/ipc/ring_buffer.c` and `src/ipc/dual_ring_buffer.c` are linked into Process A with zero callers (the vendored `sel4test-driver/CMakeLists.txt`); a cleanup prompt, not folded into anything else | `docs/CLAUDE_RECORD.md` §"Quick Reference — Shared Memory IPC" (the 2026-09-04 bracket) |
| `LOG_ERROR` redefinition | The `LOG_ERROR` macro redefinition in `nvme_log.h`, plus three `crtn.o` `.note.GNU-stack` linker warnings, survive the `-Wall` cleanup; out of scope there, on the hygiene backlog | `docs/CLAUDE_RECORD.md` §"Quick Reference — x86 Rootserver" |
| `head_dim` stack arrays (recorded here for the first time; not fixed) | `phase3/src/ai/llama_quant.c:1433` `float qn[512]; /* l_head_dim, max 512 */`, `:1442` `float kn[512]; /* l_head_dim, max 512 */`, `:1532` `float qn[512];` — stack arrays sized by a comment, indexed by `l_head_dim` (`:1184`, the per-layer `head_dim_swa` or `head_dim`); `phase3/src/ai/llama_load.c:104-109` takes `head_dim` from the GGUF's `attention.key_length` or derives `dim / n_heads`, with no bound against 512 anywhere in `llama_load.c` or `llama_quant.c`; Gemma 4's `head_dim` is exactly 512 (`phase3/src/ai/test_gemma4_config.c:219`; `head_dim_swa` 256, `:230`), so the deployed model sits at the edge. Fix shape: a load-time bound (reject `head_dim > 512`, fail closed) beside the H2 quant-type gate. Recorded as a measured line citation, not fixed | measured 2026-09-05 (`grep -n 'qn\[512\]\|kn\[512\]' phase3/src/ai/llama_quant.c`; `grep -n head_dim phase3/src/ai/llama_load.c`) |
| Coverage worth-fuzzing backlog | 274 gaps dispositioned = 150 worth-fuzzing + 102 unreachable-by-construction + 22 defensive-guard, in `phase3/scripts/coverage_judgements.json`; `gguf_parser.c` first (a directed buffer with a short length prefix opens both type switches) | CLAUDE.md row "Sanitizers + the coverage instrument (A8, 2026-08-07 …)" |
| ROADMAP B3's remaining half | The five-minute QEMU quickstart: fresh clone → one command → generated text in QEMU with no physical box. The CI generation smoke half is BUILT 2026-09-05 (manual, non-gating) | `ROADMAP.md` §Cross-phase backlog B3 |
| Two open CI decisions | (1) promote `boot-smoke`'s `generation` leg to per-push gating once it has a stability distribution (3/3 `failprobe`, 2/2 `generation` at 2026-09-05 is not one); (2) a threshold for `test_forward_compare`, which has no assertions by design and whose counts (`OK=14 WARN=0 DIVERGED=0 MATCH=8 MISMATCH=0 DIFFER=0`) were identical locally and on the runner | CLAUDE.md rows "CI boots the shipped image (A10, 2026-08-09)" and "CI model-gated suites run for REAL (2026-09-05)"; `ROADMAP.md` B3 bullet |

Sources: as per row; the `head_dim` row is the only fresh measurement on this page.

---

## 1. Strategy — what can move now, and what only the calendar or hardware can move

**Doable now, with no box week:** 7.1 (the eval corpus, the floor sweep and any embedder change are Main-PC work; only the final parity and flip touch the box); 7.5 design (a docs-first design over stores that already exist); 7.3 design (a docs-first design over the K spine and the ESP dual-boot that already exist); 7.8's pipeline (Main-PC only, zero box dependencies by its own doc).

**Calendar-bound:** 7.2 — a 30-day run of the box. Its timing is the operator's alone; this plan never proposes or dates it.

**Hardware-bound:** 7.4 — GPU inference is deferred by ADR until a usable-GPU hardware change; the box has no usable compute GPU (the R9 280X is display-only).

**External:** 7.6 — a third-party review, which no session can run or schedule.

**Last:** 7.7 — the `v2.0.0` tag closes the phase after the others, with the external audit's HIGH findings resolved before it (canon).

**The two options for the first arc — both canon now.**

*7.1 — paraphrase recall 53 % → ≥80 %.* Measurable today against the roadmap's own done-when, on an eval corpus that already exists (`cm0_recall_set.json`, ~36 distinct positives plus adversarial positives and negatives) with a scorer that already exists (`cm2_floor.py`, the pipeline that produced the 19/36 figure). Every candidate lever — the 0.55 floor, the 128-dim truncation, the mean-projection, the embedder itself — is measured off-box first, and training, if any, happens on the Main PC or cloud, never on the box. The ceiling to carry: the Phase C doc gates a 2070 contrastive fine-tune on a measured miss and nothing else, and 53 % against ≥80 % is that miss — but "0 false out of 36 is NO FAILURES OBSERVED, not a zero rate", and a recall gain bought with false recalls is not a gain.

*7.8 — the pipeline, from the owner's headset.* The owner's voice is enrolled from the Main PC headset mic and mastered first (owner-versus-not verification measured on held-out recordings). Then everything recorded is transcribed by Whisper on the RTX 2070, tagged by speaker cluster, the raw audio deleted after transcription, and distilled into a household profile in a purpose-built Main-PC memory store — who is who and to whom, the owner's style and preferences, habits, topics — shown on a clean console screen as what JARVIS currently thinks. That store is to be state of the art — the owner's stated priority, a whole phase on it if needed — and it is the same substrate 7.1's associative memory and 7.5's grounded personality need: researched before it is designed, one memory serving three goals. Only the owner is enrolled: JARVIS must work out, over days and untold, that the recurring second voice is the owner's wife and who she is to him — the challenge the owner set. Inferred facts carry a confidence and are used without confirmation; non-household speech is the owner's to purge by hand after transcription (the idea doc's §3 rule superseded, its §8). Its ceiling: it learns observable things the household said and infers relationships with a stated confidence; each shipped slice claims what was measured — never "understands you". Commands, when they come (V0), enter only through control-IN and inherit K-b — a voice command can never mint an action that is not on the static allowlist.

**First arc: the operator's choice.**

Sources: `phase4/docs/ROADMAP.md:115-122` (the goals) and `:126-133` (the done-whens); `docs/decisions/2026-06-16-defer-gpu-inference.md` (Decision); CLAUDE.md §Hardware Setup (R9 280X display only) and the "C/M2b — SEMANTIC RECALL WIRED" row (19/36, the 0.55 floor, `cm2_floor`); CLAUDE.md "Embedding Vector Store" row ("0 false out of 36 is NO FAILURES OBSERVED, not a zero rate"); `phase3/scripts/embed/README.md:14-16`; `phase6/docs/PHASE_6_GOAL_C_EMBEDDER.md:24-26,101-107`; `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md` §3, §4 (V-pipe, V0), §5.

---

## 2. Goals (canon, `ROADMAP.md:115-122`, verbatim)

1. **Associative memory (Instinct)** — Fast similarity retrieval over semantic memory (<100 MB budget). Hopfield or embedding index — retrieve relevant memories without exact query match.
2. **30-day autonomous operation** — JARVIS PC runs 30 days: proactive monitoring + inference + memory consolidation. Human checks in weekly, not daily.
3. **Self-modification (staged)** — AI-generated config/driver patches go through: sandbox → static checks → staged deploy → atomic rollback. Immutable core (kernel, SHIELD rules) never auto-modified.
4. **Larger models for hard tasks** — GPU path supports 7B+ models for complex reasoning; Gemma 4 E2B remains default for speed.
5. **Cross-session personality** — Consistent tone, remembered inside jokes, acknowledged mistakes from episodic log. Not roleplay — grounded in stored facts.
6. **External security audit** — Third-party review of memory store, SHIELD, and capability system. All HIGH findings resolved before tag.
7. **Release** — Git tag `v2.0.0` — "autonomous butler" milestone.
8. **Ambient voice wearable (household voice learning)** — Voice as JARVIS's main way of learning the owner, and later a command front-end. The owner's voice is enrolled from the Main PC headset and mastered first; then a small wearable records the household all day to local storage (radios off, USB-C batch upload) and the Main PC transcribes everything, tags speakers by voice, deletes raw audio after transcription, and distills a household profile — who is who and to whom, what is talked about, the owner's style and preferences — into a purpose-built, state-of-the-art memory store (the owner's stated priority, worth a whole phase on its own; the same substrate goals 1 and 5 need). Only the owner is enrolled: JARVIS must work out on its own, over days, that the recurring second voice is the owner's wife and who she is to him. Inferred facts carry a confidence and are used without owner confirmation; non-household speech is the owner's to delete after transcription. Recall when asked, a clean console view of what JARVIS currently thinks, and new learning in its digest; wake-word commands through control-IN come second. Pipeline first, hardware last; ALL ASR/training on the Main PC or cloud, never the box. Added to the canon 2026-09-05, shaped by the owner's decisions of 2026-09-06 (design source `BEYOND_PHASE7_VOICE_WEARABLE.md` §8); goal 7's tag still closes the phase.

Sources: `phase4/docs/ROADMAP.md:115-122`.

---

## 3. Done when (canon, `ROADMAP.md:126-133`, verbatim) + which goal satisfies each + the measured state today

| Criterion (canon) | Goal | Measured state today |
|---|---|---|
| 30-day autonomous log archived; <1% error rate, 0 crashes | 7.2 | 7.76 d unattended in one boot at `err=0`, zero restarts/faults/anomalies, ended by grid power (the 2026-08 soak) — the 30-day duration is unmet |
| Associative retrieval returns relevant memory for paraphrased queries (test suite ≥80%) | 7.1 | 19/36 = 53 % of paraphrases at the 0.55 floor; a miss degrades to the no-preamble path |
| One staged self-modification deployed and rolled back successfully in test | 7.3 | none yet — the seeds exist (§4) |
| External audit complete with no open HIGH findings | 7.6 | none yet — two internal audits: 41 fixed / 10 accepted-or-deferred / 0 open HIGH-MED |
| The owner's voice, mastered first: enrolled from the Main PC headset mic; owner-versus-not speaker verification measured on held-out recordings at a pre-registered rate; everything transcribed, raw audio deleted after transcription — proven on the pipeline before any hardware | 7.8 | none yet — no enrollment, no pipeline; the headset is the capture device |
| Household learning, the guess: with only the owner enrolled, days of recordings yield a household profile in a purpose-built, state-of-the-art memory store — who is who and to whom, the owner's style and preferences, habits, topics; each fact sourced, dated, stated-or-inferred with a confidence, used without confirmation — in which JARVIS has identified the recurring second voice as the owner's wife and who she is to him; surfaced by recall over control-IN, a clean console view, and a digest of new learning | 7.8 | none yet — no store, no recordings, no guess |
| Voice as a command front-end: one owner-voice command reaches JARVIS through control-IN and is answered (the headset first; speaker-verified from the wearable later) | 7.8 | none yet — the receiver-as-signer and control-IN exist; no voice in front of them |
| `v2.0.0` tagged | 7.7 | none yet |

Sources: `phase4/docs/ROADMAP.md:126-133`; `phase6/docs/SOAK_2026-08_FINAL_REPORT.md:13-19,27`; CLAUDE.md "C/M2b — SEMANTIC RECALL WIRED" row; CLAUDE.md §Codebase Metrics, the Security bullet.

---

## 4. Architecture seams each goal touches

**7.1 Associative memory.** The deployed semantic-recall lane: PB embeds through the co-resident Qwen3-Embedding-0.6B and hands PA a 1024-float vector over the dedicated 2-page region (`embed_region.c`); PA mean-projects and truncates to a 128-dim unit vector (`embed_project.c`, the frozen `embed_mu.h`); vectors persist per record in the JVEC store at LBA 21,150,000 (`embed_store.c`); `g3_select_semantic` compares at the 0.55 floor and `g3_build_preamble_answer_only_ex` builds the answer-only preamble with an emit mask. The eval corpus and scorer are `phase3/scripts/embed/cm0_recall_set.json` and `cm2_floor.py`. The Phase 7 canon's "<100 MB budget" and "Hopfield or embedding index" describe the target, not the present code. Rows: "C/M2b — SEMANTIC RECALL WIRED", "Embedding Vector Store", "The mean-projection pipeline", "Embed Region staleness predicate", "Phase C (Small Embedding Model …)".

**7.2 30-day autonomous operation.** The deployed 13-flag image, unchanged; the JACT store at LBA 21,120,000 as the primary evidence (per-record durable), the cumulative per-boot telemetry counters, the Pi capture ring and the daily dated snapshots (`phase6/tools/pi/`); the durable NVMe ring is the trap (it retains minutes, not days). The 6-7 run plan stays the runbook for any supervised week. Rows: the SOAK ROW, "Pi soak tooling (2026-09-04)", "Phase 6 Goal 6-7 (7-Day Supervised Exit)".

**7.3 Self-modification (staged).** The K action spine — the static allowlist (`action_allowlist.c`: the LLM selects an id, never synthesizes one), the SHIELD action gate (`shield_action.c`), the JACT audit — is the only path any patch may take; the on-SSD dual-boot (`install_jarvis_x86.sh --target esp`) with retained rollback images on the ESP is the atomic-rollback substrate; `boot-smoke` is the CI staging gate that boots the shipped image. Canon fixes the boundary: the immutable core (kernel, SHIELD rules) is never auto-modified. Rows: "Action Allowlist", "SHIELD Action Gate", "Action-Audit Store", "x86 Installer (one-script)", "CI boots the shipped image (A10, 2026-08-09)".

**7.4 Larger models.** Gated on hardware: GPU inference is deferred by ADR to a usable-GPU change (Vulkan compute named as the backend if pursued); the box's inference is memory-bandwidth-bound (~15.8 GB/s), so a bigger model on the CPU costs speed linearly. Rows: "Model options — what a bigger model would cost"; `docs/decisions/2026-06-16-defer-gpu-inference.md`.

**7.5 Cross-session personality.** The dedicated control-IN episodic store at LBA 21,140,000 (4096 records) with cross-session recall (exact-key, then semantic), the workload episodic store at LBA 21,100,000, and the semantic fact store at LBA 21,110,000 (`JARVIS_SEMANTIC` gated off, deterministic distill, write-only). Standing ceiling wording: never "remembers your conversation", never "knows preferences". Rows: `JARVIS_CONTROL_IN_RECALL`, "Episodic Store (Phase 5 G1/M0)", "Semantic Memory (Phase 5 #4/M0)".

**7.6 External security audit.** The surfaces an external reviewer would take: the control-IN security core (`phase3/src/crypto/`, `phase3/src/net/`, the query SHIELD, the SEC-014 input process), the raw-LBA stores, the action spine, the sanitised and fuzzed CI (halt-on-error UBSan, the coverage instrument). The internal record: two audits, 41 fixed / 10 accepted-or-deferred / 0 open HIGH-MED. Rows: the Security bullet, "Control-IN Security Core", "Sanitizers + the coverage instrument (A8, …)".

**7.7 Release.** The `v1.0.0` (`bdf0951`) and `v1.1.0-memory` (`feeafd1`) precedents: an annotated tag, a final report, a doc-honesty pass. Rows: the phase table; "Phase 4 Final Report".

**7.8 Ambient voice wearable.** Main-PC only for the learning: Whisper on the RTX 2070, speaker clustering and owner verification, the distill, and the purpose-built, state-of-the-art memory store — the substrate 7.1 and 7.5 share — all live there; the console (`phase4/console/`) gains a profile screen — designed clean in Claude Design against the console's design system — and a learned-this-week digest under the UI–feature-parity rule (a real live source, never hardcoded). The box's only new surface is distilled facts arriving over channels that already exist — statements over control-IN through the receiver-as-signer (`telemetry_receiver.py --send`), recalled later exactly as prior control-IN turns are — and, later, a digest count; the semantic fact store (Phase 5 #4, gated off) is a possible box-side landing for typed profile facts, a design question for the arc plan, not decided here. Commands enter through the same receiver-as-signer into control-IN and inherit HMAC + replay floor + rate limit + query SHIELD + K-b. Rows: "Telemetry Receiver / SSE bridge", "Telemetry Console", `JARVIS_CONTROL_IN`, `JARVIS_CONTROL_IN_RECALL`, "Semantic Memory (Phase 5 #4/M0)", the §Technology Stack Claude Design bullet; `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md` §5–§6, §8.

Sources: the CLAUDE.md rows named per paragraph; `phase4/docs/ROADMAP.md:115-122`; `docs/decisions/2026-06-16-defer-gpu-inference.md`; `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md`.

---

## 5. Build order (proposed; keystone-first; the first arc is the operator's)

0. **The operator picks the first arc** — 7.1 Arc A or the V-pipe (§1). Nothing below starts before that choice.
1. **The chosen first arc**, plan-first as every Phase 5 and Phase 6 goal was: a goal doc, pre-registered outcomes, host measurement before any box change, a gated flag if it touches the image, a supervised flip only with box proof.
1b. **7.8 in its board order** — the owner's voice → the transcribe-everything pipeline → the memory research → the store and the guess → the console view → the digest → V0 → V1 → V2; V3 decided last. The memory research and the store are shared with 7.1 and 7.5. Whether 7.8 or 7.1 goes first is item 0.
2. **7.5 design and 7.3 design, docs-first** — both build on stores and spines that already exist; neither needs the box until a gated implementation exists.
3. **7.2** — when, and only when, the operator schedules it; run from the 6-7 run plan's discipline (JACT primary, capture on the Pi, `err=0` the spine).
4. **7.4** — on a usable-GPU hardware change, per the ADR's own trigger; otherwise it stays blocked.
5. **7.6** — the external engagement, before the tag, with every HIGH finding resolved.
6. **7.7** — the `v2.0.0` tag, last.

Sources: `phase4/docs/ROADMAP.md:115-122,126-133`; `docs/decisions/2026-06-16-defer-gpu-inference.md` ("Revisit GPU inference only on a usable-GPU hardware change"); `phase6/docs/PHASE_6_GOAL_6-7_SOAK.md` §3, §7.

---

## 6. Locked decisions inherited

- **Training and ASR happen on the Main PC (RTX 2070) or cloud — never on the box.** The box is a CPU-only seL4 appliance that receives distilled facts, not audio and not transcripts in bulk.
- **Commands enter only through control-IN.** Voice, or any new front-end, becomes another signer in front of the existing HMAC + replay-floor + rate-limit + query-SHIELD channel and inherits K-b: it can never mint an action that is not on the static allowlist. No new inbound path to the box.
- **The audio rule, as the owner set it (idea doc §8, superseding §3's discard-before-storage):** everything is transcribed; raw audio is deleted after transcription; non-household speech is the owner's to delete by hand after transcription — a weaker mitigation than §3, recorded as the owner's accepted risk; the pipeline makes that purge one clean action. Only the owner is enrolled.
- **Honest-ceiling wording is inherited verbatim:** semantic recall reaches about half of paraphrases (19/36 at the 0.55 floor) and a miss degrades to the no-preamble path; the routing veto cut one defect class (32 → 6 at 1 FN) and "routing is fixed" is never written; the query SHIELD refuses defined abuse classes and is not a general injection detector; SEC-039 is closed for the ACTION path and control-IN queries while the workload PA↔PB lane stays passive/ALLOW by design; the running kernel configuration is outside seL4's verified X64 set.
- **The 30-day run's timing is the operator's.** Never proposed, never dated, by any session.
- **The status board rule (§0)** — a cell reads done only with the commit hash and date, written in the commit that lands the work or in the flip commit immediately after it that cites the landing hash — never in a later session.
- **Goal 8 is canon since 2026-09-05 by the operator's decision, shaped by his 2026-09-06 answers (idea doc §8); its V3 stage is decided last, never assumed.**

Sources: `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md` §3, §5; CLAUDE.md rows "C/M2b — SEMANTIC RECALL WIRED", `JARVIS_ROUTE_VETO`, "Control-IN Query SHIELD", the Phase 6 table row, the §Architecture footnote, and the "Neural future" rule recorded in the "Phase C (Small Embedding Model …)" row; `docs/decisions/2026-09-05-close-phase-6-defer-supervised-exit.md` (What would trigger the supervised run).

---

## 7. Risks

- **7.1 may not reach 80 % on paraphrases without changing what is measured.** The deployed lane sits at 53 % with 0 false recalls observed out of 36 — an upper bound of roughly 8 % on the false-recall rate at 95 %, not zero. Raising recall by lowering the floor trades against false recall, which today's evidence cannot bound tightly; a model or training change is the honest lever, and it is measured off-box first.
- **7.2 is bounded by the box's demonstrated envelope, not by the plan.** 7 d 18 h 18 m in one boot at `err=0` is the measured record; a 30-day run is ~3.9× it, and a grid outage, not the box, ended the last one. The recoverability trade (the box returns to Ubuntu) restarts the clock.
- **The whole stack runs outside seL4's verified X64 configuration** (`KernelFastpath=ON` + XSAVE + SMP). Any 30-day result is empirical stability, not a verified-kernel result.
- **7.3 is the most dangerous goal and has only seeds.** The K spine bounds actions to a static allowlist today; a self-modification path must keep the immutable core untouchable by construction, and its rollback must be exercised in test before any staged deploy.
- **7.4 has no hardware path today**; naming it a goal does not make a GPU appear.
- **7.6's cost and timeline are unknown** — an external engagement is not something a session can schedule or estimate from the repo.
- **7.8 carries a legal and ethical line, now held by hand** — continuous recording of others without consent is illegal in most Australian states; the household is two consenting adults, but the structural discard rule is superseded (idea doc §8): the mitigation is raw-audio deletion after transcription plus the owner purging non-household transcripts himself, and it depends on him doing it — his own conversations in his own home are his to record; the exposure is other people's private conversations, guests at home and anywhere the wearable is worn outside. Recorded as the owner's accepted risk; guest opt-in sessions are V3, decided last.

Sources: CLAUDE.md "Embedding Vector Store" row (the 8 % upper bound) and "C/M2b — SEMANTIC RECALL WIRED" row; `phase6/docs/SOAK_2026-08_FINAL_REPORT.md:13-19,122-125`; CLAUDE.md §Architecture footnote and `docs/decisions/2026-06-16-x86-verification-stance.md`; `phase4/docs/ROADMAP.md:117` (goal 3's immutable core); `docs/decisions/2026-06-16-defer-gpu-inference.md`; `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md` §3.

---

## 8. Boundary — what is NOT Phase 7 (canon, `ROADMAP.md:204`)

The roadmap's scope table, verbatim: *| **7** | Unbounded self-modification, cloud dependency, general AGI claims, keeping raw audio after transcription, any ASR or training on the box (the wearable's learning lives on the Main PC) |*. In addition, the wearable's V3 stage (live ambient learning — wireless or continuous variants, opt-in sessions for guests) is decided last, per its own doc, and only if still wanted after the V-pipe proves value; it is 7.8's last board row, BLOCKED until then.

Sources: `phase4/docs/ROADMAP.md:204`; `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md` §4 (V3 "Decided LAST").

---

## 9. Honest ceiling (authored)

Phase 7's title is Autonomy; its honest content today is a bounded appliance that has run 7.76 days unattended at `err=0`, recalls about half of paraphrased questions, self-heals a single inference process through a four-entry allowlist, and holds an authenticated but unencrypted conversation with one provisioned console. Nothing on the board is done. The goals that can move now are measurement and design work off the box; the goals that would make the phase's name true — a 30-day run, staged self-modification with a proven rollback, an external audit — are the operator's to schedule, the most dangerous to build, and the one nobody inside the project can run, respectively. "Autonomous" will be written on this board only beside a hash. Goal 8 is canon as of 2026-09-05 and has no code, no enrolled voice and no hardware; its first deliverable is the owner's voice enrolled from his headset, then a pipeline that transcribes everything and has to guess who else lives in the house.
