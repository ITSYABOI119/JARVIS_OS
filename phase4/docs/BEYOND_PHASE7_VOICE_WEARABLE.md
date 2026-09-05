# Beyond Phase 7 — Ambient Voice Wearable ("the bracelet")

**STATUS: IDEA STAGE — recorded 2026-09-01. Not a plan, not a commitment, no code implied.**
**PROMOTED 2026-09-05 to Phase 7 goal 8 by the operator's decision; scope set by him 2026-09-05/06 — see §8, which SUPERSEDES §3's discard-before-storage rule. This document stays the design source and still authorizes no code; the plan and its status board are `phase7/docs/PHASE_7_PLAN.md`.**
This is a Beyond-Phase-7 vision item (see `ROADMAP.md` §Beyond Phase 7). It is written down so no
future session loses it. Starting this arc is the OWNER's call, after Phase 7 exit criteria are met
(or earlier if the owner says so — the pipeline-first slice below has zero box dependencies).

---

## 1. The idea (owner's intent, verbatim in spirit)

A bracelet-style wearable that listens to speech around the owner, learns their voice and what they
talk about — "like JARVIS, he's always listening" — and eventually takes voice commands. The owner's
stated priorities, from the 2026-09-01 discussion:

1. **Ambient learning is the point**, not just commands — the device exists so JARVIS knows more
   about the owner over time.
2. Battery life of a few hours is acceptable if needed; wake-word can coexist.
3. Scope: **the owner's own voice, mainly at home.**
4. Transport idea (owner's): no live link at first — the bracelet **records to local storage like a
   USB drive**, and is plugged into the Main PC over USB-C to upload.

## 2. Two premise corrections (agreed in discussion — they make the idea EASIER)

- **Learning the owner's voice does NOT need 24/7 capture.** Speaker recognition is an ENROLLMENT
  problem: ~30 seconds to a few minutes of clean speech trains a speaker-verification embedding.
  The 24/7 half serves only ambient topic-learning.
- **Continuous capture is battery-cheap; STREAMING is what kills wearables.** An ESP32-S3-class
  device recording I2S mic audio to microSD with all radios OFF draws ~20–40 mA → a ~500 mAh cell
  runs a full waking day. 16 kHz mono ≈ 115 MB/hour raw, so a 32 GB card holds weeks. The owner's
  USB-C batch-sync instinct is therefore the RIGHT v1 architecture, not a compromise: no wireless
  attack surface, no live protocol, nothing touches the box in real time.

## 3. The consent line (non-negotiable design rule, not a disclaimer)

Continuous recording of OTHER people's private conversations without consent is illegal in most
Australian states (listening-device laws) and many other jurisdictions. The agreed mitigation is
an **INGEST RULE, implemented in code**: the Main-PC pipeline runs diarization + speaker
verification against the owner's enrolled voice, **discards all non-owner speech BEFORE anything is
stored**, and **deletes raw audio after transcription**. "Just my voice at home" is enforced by the
pipeline, not promised by the owner. A wake-word-only command device stores no third-party speech
by construction.

## 4. Staged arc

| Stage | What | Hardware |
|-------|------|----------|
| **V-pipe (first, provable now)** | The ingest pipeline: audio file → Whisper ASR (RTX 2070) → own-voice filter (diarization + speaker verify vs the enrolled voice) → transcript → topic/semantic distill → JARVIS memory. Capture-device-agnostic — provable with a cheap voice recorder or an old phone before ANY hardware is designed. **The pipeline is the reusable asset; the bracelet is a peripheral.** | none new |
| **V0 commands** | Desk mic + Whisper → the existing receiver-as-signer → **control-IN**. Proves voice→JARVIS end to end with zero new hardware. | none new |
| **V1 recorder wearable** | Offline recorder: ESP32-S3-class + I2S mic + microSD + USB-C mass-storage/charge. Radios off. Nightly plug-in → V-pipe ingest → wipe card. | new (small) |
| **V2 speaker-verified commands** | Wake-word on-device (mW-class micro model) → captured utterance → V-pipe verify (only the OWNER's voice may command) → control-IN. | V1 + firmware |
| **V3 ambient learning, live** | Only if still wanted after V-pipe proves value: wireless/continuous variants, opt-in sessions for guests, etc. Decided LAST. | TBD |

## 5. Hard rules this arc inherits (already-standing project law)

- **ALL training/ASR on the Main PC or cloud — NEVER on the box** (recorded rule, 2026-07-25). The
  box is a CPU-only seL4 appliance; it receives DISTILLED facts, not audio, not transcripts-in-bulk
  (its semantic store is 4096 × 512 B records — a distillation target, not an archive). The corpus
  lives on the Main PC.
- **Commands enter ONLY through control-IN** — voice becomes another signer in front of the existing
  HMAC + replay-floor + rate-limit + query-SHIELD channel, and inherits K-b: a voice command can
  never mint an action that is not on the static allowlist. No new inbound path to the box.
- Honest-ceiling wording discipline applies: this learns **observable things the owner said** —
  never "understands you", never "knows your preferences".

## 6. What this is NOT

Not a always-on cloud assistant; not a surveillance archive of other people (§3 forbids it
structurally); not a box feature (the box's only new surface would be distilled facts arriving over
the already-existing channels); not started — nothing in this document authorizes code.

## 7. Pointers

- Owner-side memory note: `project-wearable-voice-idea` (session memory, 2026-09-01).
- Related: `ROADMAP.md` §Beyond Phase 7 ("Mobile / edge", "Distributed JARVIS"); the control-IN
  security arc (`phase6/docs/PHASE_6_GOAL_6-5_CONTROL_IN.md`) — the door voice commands would use.

## 8. Scope set by the owner, 2026-09-05 and 2026-09-06 (promoted to Phase 7 goal 8 on 2026-09-05)

Recorded from the owner's own words after the Phase 7 home landed. Where this section and §1–§7
disagree, this section wins; §1–§7 stay as the 2026-09-01 record.

- **The owner's voice first.** Enrolled from the Main PC headset mic and mastered — owner-versus-not
  speaker verification measured on held-out recordings — before any household learning. The first tests
  need no phone and no hardware: the headset is the capture device.
- **Household, and a deliberate challenge.** The household is the owner and his wife; she has consented.
  She is NOT enrolled: JARVIS is not told who the second voice is and must work out, over days of
  recordings, that the recurring second voice is the owner's wife and who she is to him — from how often
  it is heard, daily habits, what the two talk about and how they talk to each other. The owner named
  this as the challenge he wants ("guessing please, I want to challenge it"; "this will take more than a day").
- **Continuous recording is wanted**, for topic learning "and above". That is the V1 recorder as §4
  designs it — all-day capture to the card, radios off, nightly USB-C upload; streaming stays off (§2).
- **Everything is transcribed; nothing is discarded before storage.** The §3 rule "discard non-owner
  speech BEFORE anything is stored" is SUPERSEDED by the owner on 2026-09-06: the pipeline transcribes
  all speech and tags each segment with a speaker cluster; raw audio is deleted after transcription;
  speech that is neither the owner's nor his wife's is the OWNER'S to delete, by hand, after transcription
  ("if it isn't any of my and wife's voice then I will delete it"). Recording his own conversations in his own home is
  the owner's to do ("I'm not allowed to record in my own home I've bought?"); the legal exposure §3 named
  is other people's private conversations — guests at home, and anywhere the wearable is worn outside —
  and that is exactly what the manual purge covers. A weaker mitigation than §3's structural rule, because
  it depends on the owner doing it — recorded here as the owner's accepted risk, not hidden. The pipeline must make that purge one clean
  action per speaker cluster.
- **What it learns and produces:** a HOUSEHOLD PROFILE in a purpose-built store on the Main PC — who is
  who and to whom, the owner's style and preferences, habits and routines, topics, how each person
  speaks — every fact carrying its source recording, date, stated-or-inferred and a confidence. Inferred
  facts are USED without waiting for the owner to confirm them; correction is possible, never required
  ("I couldn't be bothered proof-reading a book"). The store is to be STATE OF THE ART — the owner's words:
  "I want it to be like state of the art, I don't care if we spend a whole phase working on the one thing";
  "even if we have to make a database for this or create something new that's never been thought of". It
  is researched before it is designed (what the best agent-memory systems do today, with pre-registered
  questions), and it is the same substrate goal 7.1 (associative memory) and goal 7.5 (cross-session
  personality) need — one memory, three goals.
- **How it is surfaced, all three required:** recallable by JARVIS when asked (over control-IN); visible
  on the console as what JARVIS currently thinks — and it must look CLEAN (designed in Claude Design
  against the console's design system); and surfaced proactively in a digest of new learning.
- **Where it lives:** learning (Whisper, speaker clustering and verification, the distill, the profile
  store) on the Main PC; the box receives distilled facts only. §5's hard rules are unchanged.
- **Form factor is open.** The bracelet was the first idea; any small wearable will do; the headset comes
  first, the wearable last.
- **Honesty:** the GOAL says "learns the owner's style and preferences"; each shipped slice claims only
  what was measured. The §5 wording discipline stands.

Sources: the owner, 2026-09-05 and 2026-09-06 (the strategist session); `ROADMAP.md` §Phase 7 goal 8;
`phase7/docs/PHASE_7_PLAN.md` §0.
