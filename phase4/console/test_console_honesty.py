#!/usr/bin/env python3
"""
test_console_honesty.py - honesty gate for the N-c-3b/c telemetry console.

Greps the AUTHORED console source in phase4/console/ for (a) banned fiction
substrings that must never reappear and (b) honest-framing markers that must
stay present. Enforces the honesty rules of the headless-appliance ADR
(docs/decisions/2026-06-21-adopt-headless-appliance-remote-console.md): the
console renders only real, box-sourced telemetry - no fabricated metrics,
no GPU/tok-s-as-deployed/model-tiers/SHIELD-blocking/"formally verified".

SCOPE: the authored console files only (index.html + *.jsx + telemetry.js).
The vendored design-system runtime (_ds_bundle.js, styles.css, tokens/*.css)
is EXCLUDED: it is a shared third-party library that still carries the *other*
kit's demo data (558ms GPU, model tiers, "Ask JARVIS", ping 8.8.8.8, ...) which
the telemetry console never instantiates. Scanning it would be a false positive.

Run: python3 phase4/console/test_console_honesty.py  (nonzero exit on FAIL)
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# Vendored design-system runtime - NOT authored here, excluded from the scan.
VENDORED = {'_ds_bundle.js'}
SCAN_EXTS = ('.jsx', '.js', '.html')

# (a) Fiction that must NEVER appear in the authored console (case-insensitive).
#     Each is known-absent today; any reappearance is a regression to catch.
BANNED = [
    "GPU", "RTX", "formally verified", "558", "3.4 GB", "3.4GB", "2.1 s round",
    "tier swap", "model tier", "IDLE/ACTIVE", "approval required", "risk 0.",
    "risk score", "blocked rm", "Ask JARVIS", "<textarea", "ping 8.8", "1,284 msg",
    # #5/M2: SHIELD failure-learning is MONITOR-ONLY — no live-blocking claims, ever.
    "SHIELD blocks", "blocking active",
    # #4/M2: semantic memory compacts OBSERVABLE patterns — never a preference/intent claim.
    "knows your preferences",
    # 6-1/M3: monitors_fired is a NEUTRAL event count (a mix of degradation + benign liveness
    # events) — never a health verdict or an "anomalies/problems detected" claim.
    "anomalies detected",
    "problems detected",
    "system unhealthy",
    # truth-audit 2026-07-09: the wire last_text is the response HEAD (main_x86.c copies
    # full_response[0..47]) — the old "truncated tail" framing was WRONG; it must not return.
    "truncated tail",
    # retrieval = hits + latency only; a "memory helped" system claim is banned.
    "memory helped",
    # the old Phase-1 harness claim — the live query path is passive ALLOW.
    "100% harmful blocked",
    # no CPU-load fiction (the rootserver busy-polls; a load % would be meaningless).
    "cpu%", "cpu load",
    # no fabricated disk metrics (the box reports none).
    "IOPS", "SMART",
    # the deployed box is TWO processes (A rootserver + B inference), not one.
    "single-process",
    # 6-2/M3: a wake is an event-triggered CONSULT of a fixed, human-reviewed template —
    # never cognition. ("autonomous" is NOT banned globally: the K/M3 ACTIONS row legitimately
    # says "Self-healing / autonomous actions" of the bounded, allowlisted action gate — but it
    # IS banned inside the 6-3 Proactive-behaviors card, checked scoped below.)
    "thinking",
    "reasoning",
    "decided on its own",
    # 6-3/M3: a behavior is a registry INFORM — the box never "decides" anything.
    "the AI decided",
    # 6-5/M3-4a: control_in_blocked is a DEFINED-ABUSE-CLASS refuse count. The console must NEVER
    # claim the box detects/blocks injection or attacks — general injection is contained STRUCTURALLY,
    # not detected. (The honest framing "defined abuse class" / "not detected" is REQUIRED below.)
    "injection blocked",
    "attacks blocked",
    "threats detected",
    "prevents injection",
    "detects malicious",
    "malicious queries stopped",
    "blocks injection",
    # 6-5/M3-4b: the control-IN SEND surface. The channel is authenticated inbound and
    # CRC'd (NOT signed) outbound — "secure channel" overclaims it; and the box refuses a
    # defined abuse class, it does not stop attacks.
    "secure channel",
    "stops attacks",
]

# (b) Honest-framing markers that MUST stay present somewhere in the console
#     (case-insensitive). Proves the honest scaffolding wasn't stripped.
REQUIRED = [
    "not a blocker",
    "ALLOW",
    "no queries yet",
    "collecting",
    "SIMULATED",
    "response head",           # last_text is the response HEAD (truth-audit 2026-07-09)
    "vacuous",                 # the 2-of-5-vacuous self-test caveat must stay on-screen
    "5.46",
    "not the deployed build",
    "human-reviewed question",  # 6-2/M3: the wake consult framing (consults, not cognition)
    "informs you",              # 6-3/M3: the proactive-behavior framing (informs, not decisions)
    "defined abuse class",      # 6-5/M3-4a: control_in_blocked is an abuse-class refuse count...
    "not detected",             # ...and general injection is contained STRUCTURALLY, not detected
]

# At least one spelling of the verification-stance marker must be present.
REQUIRED_ANY = [("functional-but-unverified", "functional-unverified")]

_PASS = 0
_FAIL = 0


def check(cond, msg):
    global _PASS, _FAIL
    if cond:
        _PASS += 1
        print("  PASS: %s" % msg)
    else:
        _FAIL += 1
        print("  FAIL: %s" % msg)


def main():
    print("== telemetry console honesty gate ==")
    files = sorted(f for f in os.listdir(HERE)
                   if f.endswith(SCAN_EXTS) and f not in VENDORED)
    blobs = {}
    for f in files:
        with open(os.path.join(HERE, f), 'r', encoding='utf-8', errors='replace') as fh:
            blobs[f] = fh.read()
    corpus = "\n".join(blobs.values()).lower()

    # sanity: the authored set really is what we scanned
    check(len(files) >= 8, "scanned >= 8 authored files (%d: %s)" % (len(files), ", ".join(files)))
    check('_ds_bundle.js' not in files, "vendored _ds_bundle.js excluded from scan")
    for must in ('index.html', 'telemetry.js', 'ConsoleCommandCenter.jsx', 'ConsoleShield.jsx'):
        check(must in files, "scanned %s" % must)

    # (a) banned fiction is absent
    for b in BANNED:
        hits = [f for f, t in blobs.items() if b.lower() in t.lower()]
        check(not hits, "banned absent: %r%s" % (b, "" if not hits else "  <-- in %s" % hits))

    # (b) honest markers are present
    for r in REQUIRED:
        check(r.lower() in corpus, "required present: %r" % r)
    for variants in REQUIRED_ANY:
        check(any(v.lower() in corpus for v in variants),
              "required (any of): %s" % " | ".join(variants))

    # special case: "19.7" (the bench-off llama.cpp/llama-bench REFERENCE figure —
    # not the JARVIS engine) may appear ONLY if the "not the deployed build"
    # caveat is also present.
    if "19.7" in corpus:
        check("not the deployed build" in corpus,
              "'19.7' present -> caveat 'not the deployed build' also present")
    else:
        check(True, "'19.7' absent (caveat conditional vacuously satisfied)")

    # --- N-c-3d: Capabilities surface (UI-feature parity, auto-populated) ---
    # It must derive rows from the LIVE telemetry flags_list (a new feature =>
    # a new flag => a new row), never a hardcoded capability array.
    check('ConsoleCapabilities.jsx' in files, "Capabilities view exists (ConsoleCapabilities.jsx)")
    cap = blobs.get('ConsoleCapabilities.jsx', '')
    check('flags_list' in cap, "Capabilities iterates flags_list (auto-pull from telemetry)")
    check('flags.map(' in cap, "Capabilities maps over the live flags (not a static array)")
    check('new capability' in cap.lower(), "Capabilities surfaces UNKNOWN flags (no static known-only list)")
    cap_banned = [b for b in BANNED if b.lower() in cap.lower()]
    check(not cap_banned, "ConsoleCapabilities.jsx banned-free%s" % ("" if not cap_banned else "  <-- %s" % cap_banned))
    check('ConsoleCapabilities.jsx' in blobs.get('index.html', ''), "Capabilities view wired into index.html")

    # --- v4: live measured tok/s — allowed ONLY as a real, captioned measurement ---
    # The throughput value must be sourced from the REAL wire field (infer_last_tok_x100,
    # RDTSC-measured in Process B), captioned live/idle honestly, and never conflated with
    # the 5.46 deployed benchmark (which stays a labeled reference line).
    cc = blobs.get('ConsoleCommandCenter.jsx', '')
    check('infer_last_tok_x100' in cc,
          "Throughput tile sources the REAL measured field (infer_last_tok_x100)")
    check('live · last inference' in cc,
          "live tok/s is captioned 'live · last inference' (never a deployed guarantee)")
    check('idle (serving from cache)' in cc,
          "idle state honestly captioned 'idle (serving from cache)'")
    check('deployed benchmark (reference)' in cc,
          "the 5.46 benchmark stays a separately-labeled reference (not conflated with live)")
    lr = blobs.get('LastResponse.jsx', '')
    check('infer_last_tok_x100' in lr and 'infer_gen_tokens' in lr,
          "LastResponse decode-speed/tokens sourced from the REAL measured fields")

    # --- #5/M2: SHIELD failure-learning — monitor-only section (real fields, honest wording) ---
    # The learned signal is allowed ONLY as a monitor: real shield_learn_* fields, flag-gated,
    # and worded "monitor-only / not a live blocker" — never a block count or a blocking claim.
    sh = blobs.get('ConsoleShield.jsx', '')
    check('shield_learn_keys' in sh,
          "Shield failure-learning sources the REAL field (shield_learn_keys)")
    check('shield_learn_max_risk_x100' in sh,
          "Shield failure-learning sources the REAL field (shield_learn_max_risk_x100)")
    check('monitor-only' in sh.lower(),
          "failure-learning section carries 'monitor-only'")
    check('not a live blocker' in sh.lower(),
          "failure-learning section carries 'not a live blocker'")
    check('feeds the live phase-6 action gate' in sh.lower(),
          "failure-learning points its learned risk at the live Phase-6 ACTION gate "
          "(the gate went live at K/M4 — never a query-blocking claim)")
    check('SHIELD_LEARN' in blobs.get('ConsoleCapabilities.jsx', ''),
          "Capabilities labels the SHIELD_LEARN flag (monitor-only row)")

    # --- #4/M2: semantic memory — distilled-fact count with the honest observable-patterns wording ---
    sysb = blobs.get('ConsoleSystem.jsx', '')
    check('semantic_fact_count' in sysb,
          "System 'Distilled facts' sources the REAL field (semantic_fact_count)")
    check('observable patterns' in sysb.lower(),
          "semantic stat carries 'observable patterns'")
    check('not stated preferences' in sysb.lower(),
          "semantic stat carries 'not stated preferences'")
    check('Semantic memory (distilled facts)' in blobs.get('ConsoleCapabilities.jsx', ''),
          "Capabilities labels the SEMANTIC flag (distilled facts row)")

    # --- K/M3: self-heal / action-gate counts — real fields, self-heal/action framing, NEVER a
    # query-block claim (actions_blocked is the SEPARATE action gate, not the passive query SHIELD) ---
    check('restart_count' in sysb and 'actions_fired' in sysb and 'actions_blocked' in sysb,
          "System 'Self-healing' sources the REAL fields (restart_count/actions_fired/actions_blocked)")
    check('self-heal' in sysb.lower() or 'self heal' in sysb.lower(),
          "self-healing stats carry 'self-heal' framing")
    check('action gate' in sysb.lower(),
          "actions_blocked stat is scoped to the 'action gate' (not the query path)")
    # The ACTIONS capability label must use self-heal/action framing, never query-block wording.
    capb = blobs.get('ConsoleCapabilities.jsx', '')
    check('Self-healing / autonomous actions' in capb,
          "Capabilities labels the ACTIONS flag (self-healing / autonomous actions)")
    # The SHIELD screen must scope its 'always ALLOW / no blocked' claim to the QUERY path so it
    # does not contradict a nonzero actions_blocked (the separate action gate).
    shb = blobs.get('ConsoleShield.jsx', '')
    check('action gate' in shb.lower() and 'query' in shb.lower(),
          "SHIELD screen scopes the passive-ALLOW note to the query path (separate action gate noted)")

    # --- 6-1/M3: always-on monitors — a NEUTRAL notification count, never a health verdict. The
    # stat must source the REAL fields and use observational "notifications/flagged" wording. ---
    check('monitors_fired' in sysb and 'last_monitor_event' in sysb,
          "System 'Monitors' sources the REAL fields (monitors_fired/last_monitor_event)")
    check('Monitor notifications' in sysb,
          "monitor stat uses the neutral 'Monitor notifications' label")
    check('flagged' in sysb.lower() and 'audited' in sysb.lower(),
          "monitor stat wording is observational (flagged + audited), not a health verdict")
    check('benign' in sysb.lower(),
          "monitor stat notes the mix includes benign liveness events (not all 'problems')")
    check('Always-on monitors' in capb,
          "Capabilities labels the MONITORS flag (always-on monitors)")

    # --- 6-3/M3: the Proactive-behaviors card — registry INFORMS, never cognition/agency. The
    # card must source the REAL v10 fields, keep the manifest in sync with behaviors.c, use
    # "informs you"/event-triggered wording, and NEVER use agency language INSIDE the card
    # (scoped: "autonomous" stays legal for the K ACTIONS row elsewhere). It must also never
    # present a combined wakes+behaviors total (the deliberate double-count is two views of
    # ONE event — documented, never summed). ---
    check('behaviors_fired' in sysb and 'behaviors_mask' in sysb and 'last_behavior' in sysb,
          "System 'Proactive behaviors' sources the REAL v10 fields")
    # The proactive scope = TWO disjoint slices: the data section (const proReported ... up to
    # the 'const stat =' helper — the manifest with its "informs you" descs lives here) + the
    # card JSX ('title="Proactive behaviors"' ... '</Card>'). Deliberately EXCLUDES the Memory
    # card in between (its ACTIONS comment legitimately says "autonomous").
    pro_start = sysb.find('const proReported')
    pro_data_end = sysb.find('const stat =', pro_start if pro_start >= 0 else 0)
    pro_jsx = sysb.find('title="Proactive behaviors"')
    pro_end = sysb.find('</Card>', pro_jsx if pro_jsx >= 0 else 0)
    pro_card = ''
    if 0 <= pro_start < pro_data_end and 0 < pro_jsx < pro_end:
        pro_card = sysb[pro_start:pro_data_end] + sysb[pro_jsx:pro_end]
    check(len(pro_card) > 0, "Proactive-behaviors card block found in ConsoleSystem.jsx")
    check('informs you' in pro_card.lower(),
          "card wording is 'informs you ...' (informs, not decisions)")
    check('event-triggered' in pro_card.lower(),
          "card wording is event-triggered (never self-initiated cognition)")
    for kw in ('autonomous', 'the ai decided', 'thinking', 'reasoning', 'decided on its own'):
        check(kw not in pro_card.lower(),
              "card block does NOT say '%s' (scoped agency-language ban)" % kw)
    check('total activity' not in pro_card.lower() and 'wakes + behaviors' not in pro_card.lower(),
          "card never presents a combined wakes+behaviors sum (two views of one event)")
    for bname in ('anomaly-consult', 'self-heal-consult', 'store-roll', 'status-digest', 'degraded-alert'):
        check(bname in pro_card, "card manifest carries the registry row '%s' (KEEP IN SYNC)" % bname)
    check('KEEP IN SYNC' in sysb,
          "the manifest carries the KEEP-IN-SYNC-with-behaviors.c marker")
    check('Proactive behaviors (registry informs)' in capb,
          "Capabilities labels the PROACTIVE flag (registry informs)")

    # --- 6-5/M3-4a: control-IN two-way channel. control_in_blocked is a DEFINED-ABUSE-CLASS refuse
    # count; the console frames it honestly (defined abuse class / not detected) and NEVER as
    # injection/attack detection (the BANNED list above). Gated off in the deploy until the M4 flip. ---
    check('Control-IN — two-way conversation (gated)' in capb,
          "Capabilities labels the CONTROL_IN flag (two-way, gated)")
    check('Abuse-class refusals' in sysb,
          "System shows the control-IN 'Abuse-class refusals' stat (not a block/threat claim)")
    check('defined abuse class' in sysb.lower(),
          "control-IN refusals stat carries the 'defined abuse class' framing")
    check('contained structurally, not detected' in sysb.lower(),
          "control-IN stat carries the 'contained structurally, not detected' honesty ceiling")

    # --- 6-5/M3-4b: the Control-IN SEND surface. This is the ONE non-read-only screen, so the
    # gate has to be strict: the composer must be GATED on the live CONTROL_IN flag (a stub panel
    # that always renders an enabled input must FAIL here), must use <input (never the banned
    # demo-kit <textarea), and must carry the abuse-class / not-detected honesty ceiling. ---
    check('ConsoleControl.jsx' in files, "Control-IN view exists (ConsoleControl.jsx)")
    ctl = blobs.get('ConsoleControl.jsx', '')
    check(len(ctl) > 0, "ConsoleControl.jsx is non-empty")
    check('ConsoleControl.jsx' in blobs.get('index.html', ''), "Control-IN view wired into index.html")
    shell_b = blobs.get('ConsoleShell.jsx', '')
    check("id: 'control'" in shell_b and 'Control-IN' in shell_b,
          "Control-IN rail item wired into ConsoleShell.jsx")
    # TEETH #1: the composer is gated on the LIVE flag, not always-on. Requires BOTH the
    # flag-predicate call AND a derived `disabled` binding — a stub panel has neither.
    check("hasFlag(rec, 'CONTROL_IN')" in ctl,
          "composer gating reads the LIVE CONTROL_IN flag (hasFlag(rec, 'CONTROL_IN'))")
    check('const enabled =' in ctl and 'store.simulated' in ctl,
          "gating predicate exists and also excludes the SIMULATED preview (no box to send to)")
    check('disabled={!enabled' in ctl,
          "the input is disabled from the gating predicate (not unconditionally enabled)")
    check('canSend' in ctl and 'disabled={!canSend}' in ctl,
          "the Send button is disabled from the same predicate + the byte cap")
    # TEETH #2: honest gated-off explanation must be on-screen for the DEPLOYED (flag-absent) box.
    check('gated off on the box' in ctl.lower(),
          "disabled state explains control-IN is gated off on the box")
    check('activates at the flip' in ctl.lower(),
          "disabled state says the send path activates at the flip (not 'coming soon' fiction)")
    # TEETH #3: the honesty ceiling, on the send surface itself.
    check('defined abuse class' in ctl.lower(),
          "Control-IN carries the 'defined abuse class' framing")
    check('not detected' in ctl.lower(),
          "Control-IN carries the 'not detected' honesty ceiling (injection contained structurally)")
    check('structurally' in ctl.lower(),
          "Control-IN says injection is contained STRUCTURALLY (the real boundary)")
    check('crc' in ctl.lower() and 'not authenticated' in ctl.lower(),
          "Control-IN states the box->console reply is CRC'd, not authenticated")
    # TEETH #3b: DELIVERY EXCLUSIVITY IS NOT PROVEN. The box-side dst_ok assertion and the wire
    # proof establish that the reply is unicast-ADDRESSED to the provisioned console; no
    # third-host negative capture exists (that is an M4 item), so "the answer comes back to this
    # console only" would assert something the project's own record marks NOT PROVEN.
    check('addressed only to this console' in ctl.lower(),
          "reply confidentiality is framed as ADDRESSING ('addressed only to this console')")
    check('never broadcast' in ctl.lower(),
          "the addressing claim is qualified with 'never broadcast' (what dst_ok actually proves)")
    for over in ('comes back to this console only', 'answers going only here',
                 'only this console receives', 'no other host'):
        # 'no other host' is allowed ONLY inside an explicit disclaimer of proof.
        if over == 'no other host':
            check(('no other host' not in ctl.lower())
                  or ('not a proof no other host' in ctl.lower()),
                  "any 'no other host' mention is an explicit NOT-a-proof disclaimer")
        else:
            check(over not in ctl.lower(),
                  "Control-IN does not overclaim delivery exclusivity (%r absent)" % over)
    # TEETH #3c: the trust marker rides EVERY reply row, not just the CRC-failure row — a good
    # CRC proves non-corruption, never authorship.
    check('crc-checked, not authenticated' in ctl.lower(),
          "every reply row carries the 'CRC-checked, not authenticated' marker")
    # TEETH #3d: a sent frame is NOT an acknowledged frame. The box drops a frame that fails its
    # sequence floor or HMAC WITHOUT replying, so a turn can legitimately never complete; the
    # panel must say so rather than pulse "awaiting" forever and imply delivery.
    check('nothing here says the box received it' in ctl.lower(),
          "a long-unanswered turn states plainly that delivery is not implied")
    # TEETH #4: the banned demo-kit composer must not sneak back in via this new surface.
    check('<textarea' not in ctl.lower(), "Control-IN uses no <textarea (banned demo-kit composer)")
    check('<input' in ctl, "Control-IN uses a single-line <input (the 172-byte wire cap)")
    check('/send' in ctl, "Control-IN posts to the receiver's /send endpoint")
    ctl_banned = [b for b in BANNED if b.lower() in ctl.lower()]
    check(not ctl_banned, "ConsoleControl.jsx banned-free%s" % ("" if not ctl_banned else "  <-- %s" % ctl_banned))
    # The reply stream must be routed separately from telemetry (a reply is NOT a packet).
    tj = blobs.get('telemetry.js', '')
    check("'control_reply'" in tj,
          "telemetry.js routes the control_reply record kind explicitly")
    check('ingestReply' in tj and 'state.replies' in tj,
          "telemetry.js keeps replies in their OWN ring (never state.latest / connState)")
    check("flags.push('CONTROL_IN'" not in tj and 'CONTROL_IN\'' not in tj.split('startSimulator')[-1],
          "the built-in simulator does NOT claim CONTROL_IN (preview shows the DISABLED composer)")
    # The shell must no longer claim the whole console is read-only now that a send path exists.
    check('Read-only telemetry console' not in shell_b,
          "stale 'Read-only telemetry console' claim removed from the shell (a send path exists)")
    check('no composer, no control-in' not in shell_b.lower(),
          "stale 'no composer, no control-in' header comment removed from the shell")
    check('Telemetry stream is read-only' in shell_b,
          "shell scopes read-only to the TELEMETRY stream (control-IN noted as separate + gated)")

    # --- uptime: box uptime is shown ONLY as the real boot-relative uptime_ms — ≈-marked and
    # TSC-caveated (the box has no RTC), never presented as wall-clock. ---
    check('uptime_ms' in sysb,
          "System 'Uptime' sources the REAL field (uptime_ms)")
    check('tsc-derived (approximate)' in sysb.lower(),
          "uptime stat carries the 'TSC-derived (approximate)' caveat (no RTC on the box)")
    check('uptime_ms' in cc,
          "CommandCenter header uptime sources the REAL field (uptime_ms)")

    # --- truth-audit 2026-07-09: every embedded count/identity comes from the wire ---
    shell = blobs.get('ConsoleShell.jsx', '')
    check('selftest_score' in shell,
          "sidebar Process-A label sources the REAL selftest_score (no hardcoded 5/5)")
    check('selftest_score' in cc,
          "ProcessesCard self-test footer sources the REAL selftest_score (no hardcoded 5/5)")
    check('selftest_score' in capb,
          "Capabilities SELFTEST_PASS label sources the REAL selftest_score")
    check('model_name' in capb,
          "Capabilities MODEL_LOADED label sources the REAL model_name (no hardcoded identity)")
    mo = blobs.get('ConsoleModels.jsx', '')
    check('llama.cpp' in mo,
          "Models bench-off speeds attributed to llama.cpp (llama-bench) — not the JARVIS engine")

    print("\n== Results: %d PASS, %d FAIL ==" % (_PASS, _FAIL))
    return 1 if _FAIL else 0


if __name__ == '__main__':
    sys.exit(main())
