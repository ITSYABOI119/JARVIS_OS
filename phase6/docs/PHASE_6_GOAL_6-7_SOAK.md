# Phase 6 Goal 6-7 — 7-Day Supervised Exit — PLAN-FIRST

**Status: PLAN-FIRST (a RUN plan: readiness + observability + measurement + runbook). No code unless
readiness surfaces a gap. Pre-mortem-hardened 2026-07-24.**

> Line-number caveat: every `file:line` below was verified once at authoring against HEAD, but line
> numbers drift — RE-GREP the distinctive string before relying on any citation at run time.

---

## 1. Canon + done-when

Canon (`ROADMAP.md:94`): *"7-day supervised autonomy — JARVIS runs 7 days with you present: proactive
actions logged, zero unapproved high-risk actions, <5% false-positive interrupts."*

Done-when (`ROADMAP.md:99`): *"7-day test completed with SHIELD audit trail showing no Level 2+ actions
taken without approval."* → **THE DONE-WHEN IS THE JACT AUDIT TRAIL.**

**Honest scope (§8):** 6-7 is DURATION + NEW-LANE coverage. boot_id=17 (28.7 h, q=28 M, err=0,
`restart_count=0`) already proved most of the pre-control-IN stack under sustained load; 6-7 extends the
duration to 7 days AND covers the lanes that landed AFTER it — control-IN two-way, cross-session recall,
and query routing — under sustained supervised use.

---

## 2. System under test (all default-ON, deployed)

The finished Phase-6 image `a865b830` (or its committed-source rebuild): the memory stack (episodic +
shared context + retrieval + cache growth) · the K self-heal action gate · always-on monitors (v8) ·
event-driven wake (v9) · the ≥5 proactive INFORM behaviors (v10) · control-IN two-way + cross-session
recall · query routing (v12), semantic recall (v13), the routing veto (v14). Nothing new is
deployed for the soak.

---

## 3. Observability — evidence ranked correctly

- **PRIMARY (the done-when) — the JACT action-audit store** (@ LBA 21,120,000, 4096 records; the canon
  "SHIELD audit trail"). Every action is one record: `RESTART_PB` (action=1) / `NOTIFY_ANOMALY`
  (action=2) / `WAKE_CONSULT` (action=3) / `STATUS_DIGEST` (action=4), AND the DOMINANT path
  `ACTION_CONTROL_IN_QUERY` (action=5, one per control-IN query answered OR refused, TRUST_NOTIFY). The
  4096-record store is CIRCULAR → a long or degraded week CAN WRAP, so take an **OFF-BOX JACT SNAPSHOT
  at EVERY check-in**, not just at the end:
  `sudo dd if=/dev/nvme0n1 bs=512 skip=21120000 count=4097 | python3 phase3/scripts/parse_action_audit.py`.
  `action_audit.c` is flush-after-every-write (per-record durable), so a live read is complete.
- **STABILITY + TOTALS — captured v14 telemetry.** The box broadcasts v14 (276 B) at ~2 Hz at
  deployed rates (2.08 Hz measured in the 2026-08 soak; the 1 Hz keepalive is the floor) over the
  I211. All Phase-6 counters are per-boot CUMULATIVE statics and `q_errors` is monotonic, so the LATEST
  packet yields the boot's totals and `err=0` survives a capture gap. Telemetry is a ~2 Hz SAMPLE of the
  counters (not every ~0.44 s `[STATS]` window) — fine for the seconds-to-hours events that matter here.
- **SUPPLEMENTARY — the durable NVMe telemetry log** (@ LBA 4000794624, ~20 min retention, wraps under
  load) **+ the live console** (`curl /events`, or the browser at `127.0.0.1:8800`) for spot state.

---

## 4. Capture setup (the fragile part — mandate the proven path)

- **CAPTURE HOST: a DEDICATED ALWAYS-ON host, NOT the daily-driver Main PC** (its Windows Update reboots
  and sleep would silently gap the week). The Pi (`ssh pi`) is a candidate — telemetry is BROADCAST
  (255.255.255.255:51000), which WiFi receives (unlike the unicast control-IN replies, which it cannot).
  If the Main PC must be used, disable sleep + auto-reboot and say so in the report.
- **TOOL: dumpcap/tcpdump with the BPF `udp port 51000`** — NOT `telemetry_receiver.py`'s UDP :51000
  bind, which is Hyper-V-excluded (WinError 10013) on the Main PC. (The SSE bridge `curl /events` is fine
  for spot reads — it does not bind :51000.) Capture to a SINGLE complete file (or a `-b` ring sized to
  hold ALL 7 days) — budget: measured, not estimated — ~2.1 GB/month with the Pi's `-i any` ring
  (every broadcast captured once per interface, so raw counts are ~2×); ~1 GB single-interface. The
  5 GB hard cap (50 × 100 MB) held for the whole 2026-08 run. NOT a small rolling ring: it would
  discard the early days needed for per-window FP adjudication. Analyse offline via
  `telemetry_receiver.py --replay`.
- **LIVENESS: PROVE ≥1 h no-gap on the dumpcap path in readiness, AND check capture health at every
  check-in** (pcap still growing + last-frame age < N s + seq-gap count). A discovered capture gap is
  RECOVERABLE (re-establish capture; adjudicate any missed window from the JACT snapshot + the cumulative
  counters) — it does NOT void the week. Only a box reboot / power-loss restarts the clock.
- **DATED SNAPSHOTS ARE THE DURABLE ARTEFACT:** the ring restarts at `pcap00` after any restart;
  `jarvis-soak-snapshot.timer` on the Pi writes `snap_<date>.pcap` to `/home/pi/soak-snapshots/`
  (outside the ring) daily, guarded by `timeout` so a parked box costs nothing; units + script in
  `phase6/tools/pi/`.

---

## 5. Done-when measurement

- **"Zero unapproved high-risk actions" (the canon done-when) — STRUCTURAL + verified.** The deployed
  allowlist has NO Level-2+ action (verified in `action_allowlist.c`: `RESTART_PB` / `WAKE_CONSULT` =
  `TRUST_NOTIFY` (1), `NOTIFY_ANOMALY` / `STATUS_DIGEST` = `TRUST_AUTO` (0), the control-IN `action=5`
  records = `TRUST_NOTIFY` (1) — all L0–1; `TRUST_REQUEST` (2) / `TRUST_REQUIRE` (3) exist in the enum
  but have NO deployed allowlist entry). So "no L2+ without approval" is met BY CONSTRUCTION; the JACT
  trail across all check-in snapshots CONFIRMS every record is `AUTO`/`NOTIFY`, zero `REQUEST`/`REQUIRE`.
  **HONEST FRAMING:** supervision-for-approval has no object — there is nothing to approve; the claim is
  *"the deployed action set is bounded L0–1 and the week confirms nothing escaped it,"* not *"the human
  approved N high-risk actions."*
- **"Proactive actions logged"** — `behaviors_fired` (v10) + the JACT action=2/3/4 records. On a healthy
  box these are ~only the B4 uptime digests (the 1 h / 24 h / 7 d marks) — they fired because their
  condition held AND are in the JACT trail. boot_id=17 already showed the shape (2 uptime marks over
  28.7 h, honest-0 elsewhere).
- **"<5% false-positive interrupts" — report HONESTLY, do not manufacture a percentage.** An INTERRUPT =
  a PROACTIVE inform reaching the owner (NOT an owner-initiated control-IN `action=5` — those are
  excluded; the owner asked). On a healthy week the number of informs N ≈ 3–6 (mostly uptime digests),
  so "<5%" is not statistically demonstrable — "<5% of ~5" is a zero-tolerance bar (1 FP = 20%).
  **REPORT: "K unexplained interrupts over N observed (K/N)"; PASS = K == 0; state N and that the 5%
  bound needs a larger sample.** Cite the 6-3 anti-spam gates (G1 fire-once, G2 the 6/hr global cap, G3
  the measured healthy FP baseline 0/1) as the MECHANISM proof that the rate is bounded. Optional (muddies
  "autonomy" — NOT recommended in-run): one bounded induced-degradation probe window for FP teeth.
- **`err=0` over the week** — `q_errors == 0` in every captured window + the final packet (the stability
  spine; the single most load-bearing number).
- **Separately observed (not canon, recorded):** routing correctness on the owner's REAL control-IN
  queries (watch for the 6-6 DECLINE-FP class — a genuine question wrongly declined; the very defect the
  supervised 6-6 flip caught) + recall on repeats. These are owner-initiated, not interrupts — but a week
  of real use is the honest LARGER validation set the 73-item keyword-blind suite could not be.

---

## 6. Readiness checklist

- [ ] Deployed image = the finished Phase-6 image (all default-ON), config gate verified (SMP NN=6,
      XSAVE fs7, IOMMU off, KernelFastpath=ON, every `*_PROBE`=0, `BOOT_LOG`=0); one control-IN
      round-trip (SYSFACTS live / DECLINE / INFER + a recall repeat) + an `err=0` baseline BEFORE the
      clock starts.
- [ ] Dedicated always-on capture host chosen; dumpcap `udp port 51000` → single file PROVEN ≥1 h
      no-gap; disk budget sized; the JACT snapshot command tested off-box (a real
      `dd | parse_action_audit.py` round-trip).
- [ ] JACT read confirmed (non-blocking): `action_audit.c` is per-record flush → a live 7×24 h read is
      complete. (NAND cold-power-loss durability matters ONLY for the forensics of a power-loss abort,
      which restarts the clock anyway — a non-blocking caveat, NOT a gate. Downgraded from the drafting
      pass's "hard start blocker".)
- [ ] Rollbacks retained (the current image `ba94eb04…` (boot 55) with `2c061aec…` retained as
      `.bak-pre-provenance` in BOTH locations; whether the pending -Wall commit `3f676a2` is
      deployed first is the operator's call); Ubuntu `BootOrder[0]`;
      the owner's presence model agreed (available for stability watch + real control-IN + FP judging —
      NOT 24/7, and nothing to approve).
- [ ] Store lifetime baselines RE-READ on the parked box immediately before the run (they move at
      every JARVIS boot; the 2026-09-01 figures went stale at boot 55): workload episodic
      @21,100,000 and control-IN @21,140,000 header totals, JACT total.
- [ ] NVMe SMART baseline RE-READ immediately before the run (`sudo nvme smart-log /dev/nvme0n1`):
      percentage_used, unsafe_shutdowns, media_errors, power_on_hours, data_units_written; diff the
      same fields after the run. **2026-09-04 reference values** (`nvme-cli` 2.8 installed on the
      box's Ubuntu that day for this purpose — read-only for JARVIS: nothing under `EFI/jarvis`, no
      LBA writes, no boot change) — Lexar SSD NM790 2TB, fw `18950`: `percentage_used` **1%**,
      `available_spare` 100% (threshold 10%), `media_errors` **0**, `num_err_log_entries` 0,
      `critical_warning` 0, temperature 35 °C, `power_on_hours` 9542, `power_cycles` 580,
      `unsafe_shutdowns` **160**, `data_units_written` 22,686,771 (11.62 TB), `data_units_read`
      27,176,254 (13.91 TB). The 160 unsafe shutdowns are the expected shape for a box that is
      power-cycled out of bare-metal seL4 (there is no clean shutdown path) — NOT a count of the
      2026-08-21 outage, and not a defect. What the run needs from this baseline is the endurance
      picture: 1% used with zero media errors, so a month of writes is not a wear risk.
- [ ] Pi: `jarvis-soak-snapshot.timer` active (daily 03:00, `Persistent=true`); capture service
      active+enabled; the previous run's ring archived OUT of `/home/pi/soak/` with a sha256
      manifest.

---

## 7. Run runbook

- **START:** boot JARVIS; note `boot_id`, start uptime, start JACT record count; start the dumpcap
  capture + verify it is growing. A self-heal RESPAWN of PB is EXPECTED and is NOT a reboot (per-boot
  counters survive it; the respawn is a durable JACT `restart` record) — do not restart the clock for one.
- **48 h CHECKPOINT GATE:** `err=0`, counters sane, memory stable, capture alive, JACT snapshot taken →
  only THEN commit to the full week. (The 28.7 h boot_id=17 is the prior envelope; a 7-day single boot
  is ~6× it, so the checkpoint de-risks betting the week on an unproven envelope.)
- **CHECK-IN (1–2×/day):** `err=0`; no unexpected `[ANOMALY]`/`[RESTART]` storm; counters advancing
  sanely; CAPTURE HEALTH (pcap growing, last-frame fresh, seq-gap count); take an OFF-BOX JACT SNAPSHOT
  (guards against a degraded-week wrap); send a few REAL control-IN queries (routing + recall in real
  use).
- **PASS = 7 days CUMULATIVE uptime**, stitched across any bounded, audited reboots (counters are per-boot
  cumulative → sum the finals; the NVMe stores persist — reboot-survival is proven). **ABORT / RESTART
  THE CLOCK** = a box reboot / power-loss, OR a sudden PERMANENT telemetry blackout with NO `[RESTART]`
  (the K/M4 Outcome-B undetectable hard same-core loop → power-cycle). A capture gap ALONE is RECOVERABLE
  (the JACT trail + the cumulative totals survive it).
- **COMPLETION:** a live JACT read + the stitched capture; compute §5; write the **6-7 FINAL REPORT** (the
  `PHASE_6_GOAL_6-5_FINAL_REPORT.md` precedent) with real numbers. Return the box to Ubuntu (the soak
  deploys nothing new).

---

## 8. Honest limits (record, do not gloss)

- **SUPERVISION-FOR-APPROVAL HAS NO OBJECT** — there is no deployed L2+ action to approve. This is an
  honest structural STRENGTH, but it must be NAMED, not dressed up as "the human approved high-risk
  actions."
- The workload is a SYNTHETIC PRNG load generator; control-IN is the only REAL interaction
  (owner-driven, sparse). Claim *"7 days STABLE under sustained synthetic load + real supervised
  control-IN use,"* NOT *"7 days of real-world autonomy."*
- The <5% FP bound is NOT statistically demonstrable in one supervised week (the denominator is tiny);
  report K/N raw and say so.
- "7 days" = 7 days CUMULATIVE uptime — reboot-survival is real, but per-boot counters reset, so the week
  may be stitched across bounded audited reboots.
- 6-7 GRADUATES Phase 6, and the WHOLE STACK runs OUTSIDE seL4's verified X64 configuration
  (KernelFastpath + XSAVE + SMP; the x86-verification ADR). The soak is an EMPIRICAL stability result,
  NOT a verified-kernel one. Put this in the FINAL REPORT.

---

## 9. Deliverable

This goal doc + a **6-7 FINAL REPORT** after the run. No code unless readiness (§6) surfaces a gap. If
readiness is clean, 6-7 is a RUN, not a build — and the LAST Phase-6 goal. Phase C (the embedding arc,
6-6 §8 — a small contrastively-trained embedder, trained off-box on the RTX 2070, run on the box CPU)
follows the graduation.

---

*Companion to `phase4/docs/ROADMAP.md` (goal #7) and the Phase-6 goal docs. PLAN-FIRST — authored
2026-07-24 from a grounding draft + a 4-lens adversarial pre-mortem (folded); no code, flag, or wire
change unless readiness surfaces a gap. RE-GREP every `file:line` before relying on it.*
