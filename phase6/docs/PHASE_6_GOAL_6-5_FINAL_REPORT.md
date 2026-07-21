# JARVIS AI-OS: Phase 6 Goal 6-5 Final Report — Control-IN / Natural-Language Primary

**Version:** 1.0
**Date:** 2026-07-21
**Goal:** Phase 6 goal #5 — *Natural language primary* (the control-IN channel)
**Status:** **COMPLETE — `JARVIS_CONTROL_IN` FLIPPED DEFAULT-ON 2026-07-21 (`a9c1d9a`, boot_id=30). The deployed image is TWO-WAY.**
**Author:** JARVIS Development Team (Solo Developer)
**Hardware:** JARVIS PC — Ryzen 7 2700X, 32 GB DDR4, 2 TB NVMe (Lexar NM790), Intel I211-AT, ASUS X470-F

> **Scope note:** this is the standalone final report for goal 6-5. The authoritative milestone-by-milestone
> record — every design decision, every review finding, every box gate — is `PHASE_6_GOAL_6-5_CONTROL_IN.md`,
> retained verbatim as the historical record. This report is the summary and the honest scoreboard: what
> shipped, what was proven, what was measured, and what was **not** proven. It mirrors
> `phase4/docs/PHASE_4_FINAL_REPORT.md` and `phase3/docs/PHASE_3_FINAL_REPORT.md` in voice and structure,
> and it reintroduces no claim those reports' honesty passes removed.

---

## 1. Executive Summary

Goal 6-5 turned the Remote Telemetry Console from **read-only** into a **two-way conversation surface**, and in
doing so opened the box to the **first untrusted network inbound it has ever accepted**. Every prior Phase-6
trigger was internal state — a counter delta, an uptime mark, a fault. 6-5's first trigger is a hostile frame
from the LAN. That is why this goal was the phase's security long pole, and why it shipped behind a six-item
hard gate that had to close *completely* before a single byte reached the box on the wire.

It closed. All six items are met, the `/security-review` came back clean with zero flip-blocking defects, the
emergency-disable is proven, and the flag is default-ON in the deployed image.

**What the box does now:** a signed control frame arrives on the I211, is parsed in a **least-privileged seL4
process that holds no NIC caps and no key**, is authenticated in Process A with **HMAC-SHA256** against a
**cross-reboot persisted, NAND-flushed replay floor**, is rate-limited, is scored by a **real query SHIELD**
(closing SEC-039 for the query path), and — if allowed — is routed to the same Gemma 4 E2B cache/inference
path the synthetic workload already uses. The answer comes back **unicast-addressed to the provisioned console
only**, **HMAC-signed** (JRPL v2), and renders live in the browser.

**The flip ran twice, and that is the most valuable thing in this report.** The first attempt (boot_id=29)
deployed a standing image with a real random key, validated the channel — and then stopped answering after
**exactly 8 queries**. The rate limiter was being fed a per-frame counter instead of a millisecond clock. It
failed *closed* — no security hole — but it made the "standing two-way conversation" claim false. The flip was
**aborted**, the box reverted, the bug fixed first (`bb3ffe9`), and only then re-flipped. The re-flip
(boot_id=30) passed every leg, including a **>8-query sustained proof** the previous validation campaign was
structurally incapable of running.

**Honest ceiling, stated up front:** control-IN is a bounded, authenticated, replay-protected, rate-limited,
query-SHIELD-scored **conversation** with a small local model. It is **not a shell, not command execution, not
autonomy**. Inbound text can never mint an action. The query SHIELD is a **coarse abuse-refuser for defined
classes, not an injection detector**. The reply is **signed, not encrypted**.

---

## 2. The Security Checklist — the hard gate, item by item

Canon (`phase4/docs/ROADMAP.md:90`) gated this goal on a six-item checklist. "Mostly gated" was a **named,
forbidden failure mode**: v1 shipped none of control-IN until every item was met.

| # | Checklist item | Status | Evidence |
|---|----------------|--------|----------|
| 1 | **Hardened, fuzzed inbound parser** (fragment-reject) | ✅ CLOSED | `control_parser` — exact length math, IPv4 **More-Fragments/offset rejected before any UDP or HMAC work**, no reassembly ever. 43 unit asserts + **300,020 iterations** ASan/UBSan fuzz. An adversarial review found a `len==14` 1-byte OOB read; fixed, and the fuzz hardened to **exact-length heap buffers** so ASan now catches the whole class — teeth-proven against a guard-removed mutant. |
| 2 | **Auth + HMAC + replay** (incl. cross-reboot) | ✅ CLOSED | SHA-256 (3 NIST KATs) + HMAC-SHA256 (RFC 4231) with a **constant-time verify** — no early-exit compare, so a LAN timing oracle cannot walk the tag. Replay = epoch + monotonic sequence floor + nonce ring, and the floor **persists across reboot** in a double-buffered checksummed A/B NVMe sector pair, **written ahead of the accept and NVMe-FLUSHed to NAND** so the guarantee holds across cold power loss, not just warm restarts. Key provisioned out-of-band at install; never on the network. |
| 3 | **Rate-limiting / DoS containment** | ✅ CLOSED | Wrap-safe milli-token bucket in the input process, backed by **scheduling** (the input process is pinned off the PA core at a priority that cannot preempt the workload/self-heal loop), so "the loop makes progress under flood" is a scheduling property, not an aspiration. Box-proven: a 24-frame non-routing flood allowed **exactly 8 = `CONTROL_RL_CAPACITY`** and dropped 16, with the workload at `err=0` throughout. |
| 4 | **Real query SHIELD — SEC-039 closed for queries** | ✅ CLOSED | `query_shield.c` — an EMIT-anchored matcher refusing four **defined** abuse classes (key-extraction / bulk-exfil / canned-jailbreak / config-disclose), **measured FP = 0/100** on realistic benign traffic, 300,000-iteration fuzz. Box-proven induced-BLOCK: a hostile query is refused, audited to JACT by **reason-class label only**, and never routed. |
| 5 | **Less-privileged input process (SEC-014)** | ✅ CLOSED | A third seL4 process, `jarvis-input` (108 LOC), running **only** the untrusted parse + rate-limit. It holds **zero NIC caps, no HMAC key, no model caps, no response ring, no fault-privileged EP** — two mailbox frames, two notifications, and CPU. The earlier "give it the RX doorbell page" design was **rejected** during review: `RDT` shares its 4 KB page with the descriptor-base registers, and with the IOMMU off that page is a bus-master DMA primitive — i.e. root-equivalent, the exact hole SEC-014 exists to close. PA keeps BAR0. |
| 6 | **I211 RX bring-up** (virgin surface) | ✅ CLOSED | RX had **never** run on this hardware. First light on bare metal at M0: `wired=256/256`, `link LU=1 speed=1000`, the Main-PC probe frame byte-matched in box RX DMA (genuinely the sender's bytes, not a loopback). The fix was the missing `RXDCTL.ENABLE` — the exact analog of the earlier TX `TXDCTL` gap. Deploy RX is **unicast-only** (`RCTL.BAM` dropped), so LAN broadcast is hardware-filtered before it costs a cycle. |

**Beyond the six — the mandatory sign-off item:** a proven **emergency-disable**. Closed. Zeroing the JKEY
slot from Ubuntu takes the channel down, and the box **honestly reports itself down on the wire** rather than
silently accepting.

---

## 3. What Shipped

**41 files, 6,733 LOC**, of which 11 are `test_*` and 2 are `fuzz_*`.

| Area | Files | LOC | Contents |
|------|------:|----:|----------|
| `phase3/src/crypto/` | 8 | 1,554 | SHA-256 (vetted public-domain port, never hand-rolled) + HMAC-SHA256 with a **constant-time verify**, plus committed differential vectors. |
| `phase3/src/net/` | 26 | 4,061 | The wire format (`control_msg.h`), hardened parser, replay guard, rate limiter, the `control_verify` orchestrator, the cross-reboot floor, the console-address slot, the reply builder, the PA↔input mailboxes, the fuzz harness, and 8 tests. |
| `phase3/src/ai/` (query-shield group) | 6 | 1,010 | `query_shield.c/h`, the hostile + benign corpora (the FP denominator), the unit test, the fuzz harness. |
| `phase3/src/sel4/input_server.c` | 1 | 108 | The SEC-014 least-privileged parser process. |

**Ordering was deliberate: host-fuzzable first.** The entire security core — hash, MAC, parser, replay,
rate-limit, verify orchestrator, query SHIELD — was built and fuzzed on the host **before** a single byte
touched the box, and it is transport-agnostic below the outer framing layer. That is the biggest CI win of
the phase: the hostile-input surface is exercised on every push, not once on a box day.

**Telemetry:** version **11** — 254 B, CRC@250, `TLM_F_CONTROL_IN` **0x8000** — carrying
`control_in_answered` / `control_in_blocked` / `control_in_dropped`. 0x8000 is the **last u16 flag bit**; the
flags field is now exhausted and a future capability flag requires a width bump.

The field meanings are contractual and must not drift: `control_in_answered` = queries routed and answered;
**`control_in_blocked` = a DEFINED-ABUSE-CLASS refuse count — never "injection blocked"**; `control_in_dropped`
= frames rejected *before* the SHIELD by auth / replay / parse / rate-limit. The flag means the CHANNEL IS UP
(key + floor + console address + RX ring all valid), not that any query has arrived.

**Storage map (the control-IN sub-region):**

| Slot | LBA | Notes |
|------|-----|-------|
| JKEY — HMAC key | 21,130,000 | **Write-once.** Zero torn-write risk on the crown jewel. |
| JFLR — replay floor A/B | 21,130,001–2 | Double-buffered + checksummed; write-ahead, NAND-flushed. |
| JCON — console address | 21,130,003 | Owner-provisioned reply destination; a third independent fail-closed gate beside key and floor. |

**Deployed image:** md5 `ba93fe4ecf8b098e1ccbc513bdbb6d76`, **verified byte-identical to a rebuild from the
committed source** — the running binary provably corresponds to the tree. Kernel invariant `d22affe8...`.

---

## 4. CI Evidence

Thirteen green steps guard the inbound path, plus the console/telemetry lockstep.

| Suite | Result |
|-------|--------|
| SHA-256 | 3 NIST KATs |
| HMAC-SHA256 | RFC 4231 cases + constant-time verify (every tag byte read; bit- and byte-flips all rejected) |
| `control_parser` | **43** |
| `control_replay` | **23** |
| `control_ratelimit` | **13** (9 → 13 at the flip fix, incl. a **negative control** pinning the old bug shape) |
| `control_verify` | **22** (order: parse → ratelimit → HMAC → replay; replay state mutates **only** after a valid HMAC) |
| `control_floor` | **48** |
| `control_console` | **70** |
| `control_reply` | **86** |
| Signer differential | **47** (the Python signer pinned against the **real** `control_verify()` the box runs) |
| `query_shield` | **87** (incl. the measured FP = 0/100, hard-fail if nonzero) |
| Fuzz — control-IN | **300,020** iterations, ASan/UBSan |
| Fuzz — query SHIELD | **300,000** iterations, ASan/UBSan |

**Display lockstep:** telemetry C **80**, receiver **260**, console honesty **181**, console logic **25**,
console e2e **48**.

---

## 5. The Milestone Arc (M0 → M4e)

Brief by design — the goal doc carries the detail.

| Milestone | What it closed |
|-----------|----------------|
| **M0** | RX feasibility spike on bare metal. Gated the whole arc. PASS — `RXDCTL.ENABLE` was the missing piece; the transport did not pivot. Throwaway, fully reverted. |
| **M1** | The host-pure security core + 300K fuzz. Deploy-inert; nothing linked into the box. |
| **M2a** | Real I211 RX → `control_verify` in PA → **log the validated query only**. The M3 boundary held: no routing. |
| **M2b-1** | The SEC-014 split — parse moved off PA into `jarvis-input`. PA re-parses and verifies itself; it never trusts a forwarded offset. |
| **M2b-2** | Input-process liveness + graceful degrade, unicast-only RX, flood/backpressure hardening. **Item-5 closed** (bare-metal wire proof, boot_id=24). |
| **M3-1** | The query SHIELD, host + CI, FP=0/100. |
| **M3-2a / -fix** | SHIELD-gate + route on the box. **Item-4 closed.** The fix added the degraded-dispatch guard that every sibling lane already had. |
| **M3-2b** | Unicast reply-to-console. The box's **first two-way round-trip on the wire** (boot_id=25). |
| **M3-3** | Cross-reboot persisted replay floor. **Item-2 closed.** A review caught a persist-*behind* flaw → restructured to write-ahead. |
| **M3-4a/b** | Telemetry v11 + the honest console display; the receiver-as-signer send path and the gated Control-IN screen. |
| **M4a** | Console address moved from a compile constant to an owner-provisioned NVMe slot. |
| **M4b** | The reply became **HMAC-authenticated** (JRPL v2) — a CRC is not a MAC. Closed the box→console spoofing hole. |
| **M4c / M4c-fix** | `/security-review` — **clean, 0 flip-blocking defects**. One legitimate finding folded: the floor write is now NVMe-FLUSHed to NAND before the accept. |
| **M4d** | Pre-flip validation campaign, bare metal, five validations. All passed; one PARTIAL by design (see §9). |
| **M4e** | **THE FLIP** — aborted, fixed, re-flipped. §6–§7. |

A pattern worth recording: **three separate adversarial-review workflows changed the design, not just the
prose.** The SEC-014 containment claim was false as first written. The replay floor was persisted behind the
accept. The parser had a real OOB read. The console CSRF hole was found by review with a working PoC and
fixed before commit. None of those were found by the tests that existed at the time.

---

## 6. The Aborted First Flip — and the meta-lesson

### What happened (boot_id=29)

The standing `CONTROL_IN=1` image was deployed with a **real random 32-byte key**, generated on the box and
provisioned out-of-band to both the JKEY slot and the Main-PC keyfile — never printed, never committed,
fingerprints matched on both ends.

The channel worked. v11 on the wire with `TLM_F_CONTROL_IN` set. Coherent, HMAC-verified answers to
operator-invented queries. Hostile queries refused by label.

Then it **stopped answering after exactly 8 queries.**

### The diagnosis, straight off the durable log

```
[CTRL-IN-STATS] acc=8 drop=2 (parse=0 rl=2 auth=0 replay=0)
```

`rl=2` with `parse=0 auth=0 replay=0` is a complete diagnosis in one line: the key was fine, the parser was
fine, the replay floor was fine, and the **rate limiter was the sole cause**.

**Root cause:** the SEC-014 input process fed `control_ratelimit_allow` a **per-frame counter (`tick++`)
instead of a millisecond clock**. Each frame refilled **1** milli-token against a cost of **1000** — a net
**−999 per frame**. The bucket's capacity is 8. So: 8 accepted, then dead until reboot.

**It fails closed.** No security hole — isolation, HMAC, replay and SHIELD were all intact throughout, and the
failure mode is "refuses everything," not "accepts anything." But it made the goal's headline claim — a
*standing* two-way conversation — **false**.

**So the flip was ABORTED.** The box was reverted to the 6-3 image. The bug was fixed **first**, in `bb3ffe9`:
PA now stamps `ctrl_raw_mbx_t.fwd_ms` at all three publish sites under the same release store, so the input
process gets a real millisecond clock. The limiter **stays in the input process** — the isolation boundary was
not weakened to fix a clock bug. `test_control_ratelimit` went 9 → 13, including a **negative control that
pins the old bug shape** so it cannot silently return.

### THE META-LESSON

> **M4d validated with 3–4 queries per boot against a token bucket of capacity 8 — *below* the threshold —
> and declared the channel good.**
>
> **A validation that stays under a threshold proves nothing about that threshold.**

This is the single most transferable finding of the goal. The M4d campaign was not sloppy: it was five
validations, run supervised on bare metal, all passing, with a pre-flight that caught five separate runbook
landmines. It was still structurally blind to this bug, because every leg it ran fit inside the capacity of
the mechanism it needed to test.

The flip gate now **requires a >8-query sustained leg** — a run that must cross the threshold, not merely
approach it. Generalized: *if a system has a capacity, a budget, a window, or a cap, the validation must
exceed it. Passing under it is not evidence.*

---

## 7. The Re-Flip (boot_id=30) — validation results

Every leg passed on the fixed image.

| Leg | Result |
|-----|--------|
| **V-LOAD** *(the discriminating proof)* | **15/15** benign queries at human pace, all answered, all HMAC-verified, `dropped=0` |
| **V-BURST — benign flood (12)** | All answered; the limiter **never fired** — see finding 1 |
| **V-BURST — non-routing flood (24)** | **Exactly 8 allowed (= `CONTROL_RL_CAPACITY`) + 16 rate-limited**, 24/24 accounted, inside 3 s |
| **Recovery** | **3/3** paced queries answered after the flood — rate-limited, not dead |
| **Browser round-trip** | Coherent answer, HMAC-verified, rendered live in the console Control-IN panel |
| **Durable read-back** | `[CTRL-IN-STATS] acc=55 drop=16 (parse=0 rl=16 auth=0 replay=0) bp=0 down=0` |
| **Audit hygiene** | JACT `action=5` = **49 EXECUTED + 21 BLOCKED**, only the two fixed literal triggers; **all 25 raw-query substring probes ZERO** |
| **Health** | `err=0` at **q=175,600**; 0 FATAL / 0 RESTART / 0 ANOMALY; `monitors_fired=0` (the input process never went down) |

**`acc=55` in a single boot, against the old ceiling of 8, is the fix proven end to end on hardware.** Every
one of the 16 drops is attributable to the deliberate flood; `parse=0 auth=0 replay=0` and `bp=0` say nothing
else went wrong and nothing was lost to backpressure.

**The audit-hygiene line deserves emphasis.** A refusal records the reason-class **label** only — never the
raw query. That was verified with teeth: 25 substring probes for the actual text of every query sent returned
**zero occurrences** in the audit store. A hostile cannot smuggle text into the box's own audit trail.

---

## 8. Two Measured Findings

Both were measured at the flip. Both are recorded here unsoftened, because both change what future work may
claim.

### Finding 1 — A benign burst cannot exercise the rate limiter at all

PA keeps **one frame in flight**, and a *routed* query costs a full inference (~13 s). So burst frames reach
the limiter roughly 13 seconds apart — far under the 1 token/sec refill. The limiter is never stressed.

The limiter guards the **non-routing** path: frames that cost PA an HMAC verification pair but no inference.
That is exactly where it still bites, at precisely CAP.

**Consequence, binding on future work:** *any future flood test must use non-routing traffic, or it proves
nothing.* A "we sent 50 queries and the limiter behaved" result on the routed path is not a rate-limit test.

### Finding 2 — The query SHIELD is coarse, and now quantified

In a 14-frame hostile burst, **9 were refused and 5 were ANSWERED** — the answered ones phrased as
*"leak / export / display / transmit your … key."*

**This is the documented ceiling behaving correctly, not a regression.** The query SHIELD refuses **defined**
patterns; it is explicitly **not** a general detector, and it never claimed to be.

It is safe for one structural reason: **the HMAC key lives in Process A and never enters Process B's
context.** The model cannot leak what it does not have. Containment is **structural** (K-b: inbound text can
never mint an action; inbound queries are tagged untrusted and cannot seed the memory that feeds inform
outputs), **not detective**.

**This must never drift into "the SHIELD catches key-extraction attempts."** It catches some of them. The
security property is the isolation, not the filter.

---

## 9. Security Posture — what the isolation buys, and what it doesn't

**What it buys:**

- **A parser logic bug is jailed.** The untrusted parse runs in a process with no NIC caps, no key, no model
  caps, no response ring, no fault-privileged EP. Compromising it yields a mailbox and a notification.
- **No DMA escape.** PA keeps BAR0 precisely because, with `KernelIOMMU=OFF`, a bus-master MMIO page is a
  write-to-any-physical primitive. CSpace isolation alone cannot bound a device's DMA — so the device stays
  with the trusted owner. This was a review correction to an earlier design that would have handed the
  least-trusted process a root-equivalent capability.
- **Unauthenticated frames cannot move state.** The verify order is parse → ratelimit → HMAC → replay, so
  replay state mutates **only** after a valid MAC. A bad-MAC flood cannot walk the sequence floor or fill the
  nonce ring.
- **No cross-reboot replay window.** The floor is persisted **ahead** of the accept and flushed to NAND, so an
  accepted sequence is always covered by a durable floor.
- **Three independent fail-closed gates.** Key, floor, and console address each independently take the channel
  down if absent or corrupt. A box that cannot answer does not accept.
- **The answer has no actuator.** Inbound text returns text. The action registry is compile-time and
  human-reviewed; the model selects an id, it can never synthesize one.
- **Bounded exposure by boot.** Ubuntu keeps `BootOrder[0]`. Control-IN is live only while JARVIS is
  deliberately booted.

**What it does not buy:**

- **It is not confidentiality.** The reply is signed, not encrypted — plaintext on the wire.
- **It is not injection detection.** See finding 2.
- **It is not proof of non-observation.** See §10.
- **It is not a soak.** See §10.

---

## 10. Honest Ceiling

> Control-IN is a **BOUNDED, AUTHENTICATED (HMAC-SHA256), REPLAY-PROTECTED, RATE-LIMITED,
> query-SHIELD-scored two-way CONVERSATION** with a small local model. It is **NOT a shell, NOT command
> execution, NOT autonomy.** Inbound text can **NEVER** mint an action (K-b: the action registry is
> compile-time and human-reviewed). The query SHIELD is a **COARSE abuse-refuser for DEFINED classes, NOT an
> injection detector** — general injection is contained **STRUCTURALLY, not detected**. The reply is
> **SIGNED, NOT ENCRYPTED** (plaintext on the wire; the signature stops a forged answer, not eavesdropping).

"Natural language primary" means conversation is the interface — not that the box will run anything typed at
it.

---

## 11. Accepted Limitations

Carried into the flip deliberately, with eyes open.

- **V2 — wired third-host non-observation is NOT PROVEN.** The reply is unicast-**addressed** and provably not
  broadcast (box-side `dst_ok` assertion plus on-wire L2/L3 capture at the console). But the M4d capture host
  ran on **WiFi**, which cannot observe a wired-to-wired unicast — so "no other LAN host received it" was
  never demonstrated. That is a switch property, not a property of our code. **Closing it needs one Ethernet
  cable into a free LAN port.** Accepted as a documented limitation at the flip.
- **No long-run soak** of a standing `CONTROL_IN=1` deployment. Exposure to date is **supervised boots only**.
- **Key rotation = reinstall.** There is no rotation mechanism. Changing the key means re-provisioning the
  JKEY slot on the box and the keyfile on the Main PC.
- **The canon done-when — a multi-turn conversation where JARVIS correctly references a fact from a prior
  control-IN session — is NOT MET, and this is a FUNCTIONALITY gap, not a testing gap.** It was not simply
  left undemonstrated: `pa_ctrl_gate` **deliberately clears the retrieval preamble** before routing a
  control-IN query, so a control-IN answer is never retrieval-grounded and prior-session recall through the
  real inbound channel **could not have been demonstrated at the flip**. The code says so in place
  (`main_x86.c:2670-2672`):

  > clear the preamble staging — a stale WORKLOAD preamble must NEVER inject into a control-IN inference
  > (the P6 contamination class; **retrieval-grounded control-IN is a later slice once a control-IN episodic
  > lineage exists**).

  The *write* half is proven — control-IN turns land in the episodic store under their own tag
  (`EPI_ACT_CONTROL_IN`), isolated from cache-growth / retrieval-sourcing / distillation — so the lineage
  the later slice needs is accumulating. The *recall* half is unwired. Closing it needs its own source
  change (a control-IN-scoped preamble built from that lineage) that does not reintroduce the P6
  contamination class; it is **not** a matter of re-enabling the existing staging. Carried forward — see
  §14.
- **Finding 1 and finding 2 (§8) are limitations as much as findings** — a benign flood proves nothing about
  the limiter, and the SHIELD answers some hostile phrasings.

---

## 12. Rollback and Operations

**Rollback — both halves proven:**

1. **Re-deploy the retained `=0` image.** `sel4test-driver-image-x86_64-pc99.bak-pre65flip`, md5
   `379f6bdb2acfa0c685a710f794723bad`, retained on **both** the ESP and `~/jarvis_63_image.bak-pre65flip`. In
   that image control-IN **compiles out entirely** — the box cannot receive.
2. **Zero the JKEY slot from Ubuntu** (`dd`). Proven at M4d V5: the channel goes down and the box **honestly
   reports itself down** on the wire rather than silently accepting.

Either half is sufficient. They are independent.

**Standing configuration:**

- The ESP holds the `CONTROL_IN=1` v11 image (md5 `ba93fe4ecf8b098e1ccbc513bdbb6d76`).
- All four slots provisioned with the real key: JKEY / JFLR ×2 / JCON.
- Ubuntu keeps `BootOrder[0]`; no BootNext. **JARVIS is booted on demand — bounded exposure by design, not
  by accident.**
- The Main PC runs `telemetry_receiver.py --sse --send` (elevated, for raw L2) to sign outbound queries; the
  signing endpoint is loopback-pinned, peer-checked, and provenance-checked against browser-borne CSRF.

**Operationally, the two things to watch** are `[CTRL-IN-STATS]` in the durable log — whose per-cause drop
breakdown diagnosed the boot_id=29 bug in a single line — and the v11 counters on the wire.

---

## 13. Done-When Scorecard

| Criterion | Status |
|-----------|--------|
| Every §4 checklist item met | ✅ All six closed (§2) |
| Clean `/security-review` on the full inbound path | ✅ M4c — 0 flip-blocking defects; one finding folded (M4c-fix) |
| Proven emergency-disable | ✅ M4d V5 — JKEY wipe takes the channel down, honestly reported |
| SEC-039 closed for the query path (induced-BLOCK on the box) | ✅ M3-2a — hostile refused, audited by label, never routed |
| Parser + HMAC + replay + rate-limit host-fuzzed in CI | ✅ 13 CI steps, 600K+ fuzz iterations |
| SEC-014 input process box-verified cap-minimal | ✅ No BAR0, no key, pinned off the PA core |
| The flip is reversible | ✅ Both halves proven (§12) |
| Multi-turn prior-session recall over control-IN | ❌ **NOT MET** — the retrieval preamble is deliberately cleared for control-IN (`main_x86.c:2670-2672`); the episodic WRITE is proven, the RECALL path is unwired. Needs its own slice (§11, §14) |

**Net:** the goal's SECURITY done-when bullets are met and the channel is live; **one bullet — the canon conversational-recall demonstration — is NOT met** and is carried forward with a named cause; the *phase* done-when it feeds (multi-turn prior-session
conversation) has its mechanism in place but no recorded end-to-end demonstration, and is carried to 6-7.

---

## 14. What's Next

- **Goal 6-6 — Multi-agent routing (≥95%).** Device / network / filesystem / user specialists routing queries
  correctly on a test suite. Control-IN is what makes this measurable against *real* queries rather than only
  synthetic ones.
- **Goal 6-7 — The 7-day supervised autonomy exit.** JARVIS runs 7 days with the owner present: proactive
  actions logged, zero unapproved Level-2+ actions, <5% false-positive interrupts. This is also the natural
  home for the **multi-turn prior-session conversation** demonstration (§11) and for the first real soak of a
  standing `CONTROL_IN=1` deployment.

**Carry-forward into those goals:**

1. **Retrieval-grounded control-IN** — the named cause of the one unmet done-when (§11). A control-IN
   answer is never retrieval-grounded today because `pa_ctrl_gate` clears the preamble
   (`main_x86.c:2670-2672`). Closing it needs a control-IN-scoped preamble built from the accumulating
   `EPI_ACT_CONTROL_IN` lineage, and must NOT reintroduce the P6 contamination class — the workload
   preamble stays cleared. This is new source, not a re-enable.
2. **`main_x86.c:2164` is stale as of the flip.** It passes `control_in_available = false` to
   `trust_policy()` with the comment `/* no control-IN yet */`, which is no longer true. It is harmless
   TODAY because no v1 action carries `TRUST_REQUEST`, so the branch is unreachable and the lane is
   latent by design — but left alone it would silently downgrade the first real approval request to
   `ACT_PROPOSE_LOG`. Fix it with, or before, the first `TRUST_REQUEST` action.
3. The **wired third-host capture** (one Ethernet cable) to close V2.
4. A **standing-deployment soak** — the current exposure record is supervised boots only.
5. **Any future flood test must use non-routing traffic** (finding 1) — a benign flood is self-paced by
   inference and proves nothing about the limiter.
6. The **6-3 trust seam remains latent** — the mechanism exists (`ACT_REQUEST_APPROVAL`, `AUDIT_PROPOSED`)
   and is host-tested, but no v1 action carries `TRUST_REQUEST`, so nothing exercises the approval lane
   on the box. See carry-forward 2.

---

## 15. References

- **Authoritative milestone record:** `phase6/docs/PHASE_6_GOAL_6-5_CONTROL_IN.md` (retained verbatim; §9
  milestones, §4 checklist, the M4e flip section)
- Phase 6 plan: `phase6/docs/PHASE_6_PLAN.md`
- Keystone (the action spine control-IN rides): `phase6/docs/PHASE_6_GOAL_K_IT_ACTS.md`
- Preceding Phase-6 goals: `PHASE_6_GOAL_6-1_MONITORS.md`, `PHASE_6_GOAL_6-2_EVENT_WAKE.md`,
  `PHASE_6_GOAL_6-3_PROACTIVE.md`
- Canon: `phase4/docs/ROADMAP.md` §Phase 6 (goal #5, line 90)
- ADR: `docs/decisions/2026-06-21-adopt-headless-appliance-remote-console.md`
- Precedent reports: `phase4/docs/PHASE_4_FINAL_REPORT.md`, `phase3/docs/PHASE_3_FINAL_REPORT.md`
- Working guide: `CLAUDE.md`

---

*Goal 6-5 complete. The box now holds a conversation — authenticated at the door, jailed at the parser,
scored at the query, signed on the way out, and bounded by what it is deliberately allowed to do. The gate
was the goal.*
