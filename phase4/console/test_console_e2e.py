#!/usr/bin/env python3
"""
test_console_e2e.py - Layer C: real SSE end-to-end (render + contract + flag-parity).

Boots the real telemetry_receiver.py --sse --replay golden.pcap, loads the
console in headless Chromium, and asserts it mounts (Babel+React, no console
errors), shows a LIVE connection, renders real box fields, did NOT fall back to
the simulator (false-pass guard), and — the key invariant — EVERY live
flags_list flag renders a Capabilities row (a reported feature the UI drops is a
hard FAIL). A final phase points at a static server with no /events and asserts
the SIMULATED fallback badge DOES appear.

Run: python3 phase4/console/test_console_e2e.py   (nonzero exit on FAIL)
Requires: pip install playwright==1.60.0 ; playwright install --only-shell chromium
"""

import os
import subprocess
import sys
import time
import urllib.request

from playwright.sync_api import sync_playwright, expect

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
GOLDEN = os.path.join('phase4', 'console', 'fixtures', 'golden.pcap')
PORT = 8137
BASE = 'http://127.0.0.1:%d/' % PORT
STATIC_PORT = 8141  # plain static serve (no SSE endpoint) for the fallback phase

# flag name -> a substring of the Capabilities row label it must render
# (ConsoleCapabilities.jsx FLAG_LABELS; unknown flags fall back to the raw name).
FLAG_LABEL = {
    'MODEL_LOADED': 'LLM model loaded',
    'SELFTEST_PASS': 'Self-test passed',
    'FB_DRAWABLE': 'Framebuffer drawable',
    'FB_MAPPED': 'Framebuffer mapped (on-box HUD)',
    'HAS_ERROR': 'Error state',
    'MEMORY': 'Episodic memory store',
    'CONTEXT': 'Shared context pool',
    'RETRIEVAL': 'Retrieval before inference',
    'CACHE_GROWTH': 'Cache growth — learns frequent queries',
    'SHIELD_LEARN': 'SHIELD failure-learning (monitor-only)',
    'SEMANTIC': 'Semantic memory (distilled facts)',
    'ACTIONS': 'Self-healing / autonomous actions',
}

_P = 0
_F = 0


def check(cond, msg):
    global _P, _F
    if cond:
        _P += 1
        print("  PASS:", msg)
    else:
        _F += 1
        print("  FAIL:", msg)


def wait_port(url, tries=100):
    for _ in range(tries):
        try:
            urllib.request.urlopen(url, timeout=1)
            return True
        except Exception:
            time.sleep(0.1)
    return False


def main():
    errors = []
    bridge = subprocess.Popen(
        [sys.executable, 'phase3/scripts/telemetry_receiver.py', '--sse',
         '--web-dir', 'phase4/console', '--http-port', str(PORT),
         '--replay', GOLDEN, '--replay-rate', '0.1'],
        cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    static = None
    try:
        if not wait_port(BASE):
            raise RuntimeError("telemetry bridge did not come up on %s" % BASE)

        with sync_playwright() as pw:
            browser = pw.chromium.launch()
            page = browser.new_page()
            page.on('console', lambda m: errors.append('console: ' + m.text) if m.type == 'error' else None)
            page.on('pageerror', lambda e: errors.append('pageerror: ' + str(e)))

            page.goto(BASE)
            expect(page.get_by_text('functional-unverified')).to_be_visible(timeout=15000)
            check(True, "page mounted (Babel + React over real HTTP)")

            # (v4) EARLY WATCHER for the live tok/s pin: the tok-carrying golden frame is live for
            # only ~1 s of the (non-looping) replay, so a late poll can miss it. Latch the moment
            # the Throughput tile renders infer_last_tok_x100/100 while that record is live; the
            # pin later asserts the latch.
            page.evaluate(
                "() => {"
                " window.__tokPin = { seen: false, x: null, want: null };"
                " window.__tokPinTimer = setInterval(() => {"
                "   try {"
                "     const rec = (window.JarvisTelemetry.getState().latest) || {};"
                "     const x = rec.infer_last_tok_x100;"
                "     if (typeof x === 'number' && x > 0) {"
                "       const want = (x / 100).toFixed(2);"
                "       window.__tokPin.x = x; window.__tokPin.want = want;"
                "       if (Array.from(document.querySelectorAll('span'))"
                "             .some(el => el.textContent.trim() === want))"
                "         window.__tokPin.seen = true;"
                "     }"
                "   } catch (e) {}"
                " }, 50);"
                "}")

            expect(page.get_by_text('link OK · live')).to_be_visible(timeout=15000)
            check(True, "connection pill shows 'link OK · live' (real SSE)")

            expect(page.get_by_text('Gemma 4 E2B').first).to_be_visible(timeout=10000)
            check(True, "model_name 'Gemma 4 E2B' renders")

            # UI-feature parity: the console surfaces the live model_load_pct (a 'Model load' bar)
            # mirroring the new on-box HUD progress bar (sourced from the same /events field).
            expect(page.get_by_text('Model load').first).to_be_visible(timeout=10000)
            check(True, "console surfaces live model_load_pct ('Model load' bar) — UI-feature parity")

            # real query count flows (cold-start 'no queries yet' clears at least transiently)
            saw_q = False
            deadline = time.time() + 10
            while time.time() < deadline:
                lt = page.evaluate("(window.JarvisTelemetry.getState().latest||{}).q_total")
                if lt and lt > 0:
                    saw_q = True
                    break
                time.sleep(0.1)
            check(saw_q, "real query count flows (latest.q_total > 0)")

            check(page.evaluate("(window.JarvisTelemetry.getState().latest||{}).num_nodes") == 6,
                  "num_nodes == 6")

            # an INFER frame's last_text reaches the client
            saw_text = None
            deadline = time.time() + 10
            while time.time() < deadline:
                lt = page.evaluate("(window.JarvisTelemetry.getState().latest||{}).last_text")
                if lt:
                    saw_text = lt
                    break
                time.sleep(0.1)
            check(bool(saw_text), "INFER last_text flows to the client (%r)" % saw_text)

            # FALSE-PASS GUARD: it consumed REAL /events, not the simulator fallback
            check(page.evaluate("window.JarvisTelemetry.getState().simulated") is False,
                  "simulated == False (real SSE, not the fallback)")
            check(page.get_by_text('SIMULATED · replay').count() == 0,
                  "no 'SIMULATED · replay' badge on the real-SSE page")

            # FLAG-PARITY INVARIANT: every reported flag renders a Capabilities row
            page.get_by_title('Capabilities', exact=True).click()
            expect(page.get_by_text('Reported capabilities (flags)')).to_be_visible(timeout=10000)
            parity_ok = False
            seen = None
            deadline = time.time() + 10
            while time.time() < deadline:
                snap = page.evaluate(
                    "() => { const s = window.JarvisTelemetry.getState();"
                    " const m = document.querySelector('main');"
                    " return { flags: (s.latest && s.latest.flags_list) || [], text: m ? m.innerText : '' }; }")
                flags, text = snap['flags'], snap['text']
                seen = flags
                if flags and all((FLAG_LABEL.get(f, f) in text) for f in flags):
                    parity_ok = True
                    break
                time.sleep(0.12)
            check(parity_ok,
                  "flag-parity: every live flags_list flag renders a Capabilities row (flags=%s)" % seen)

            # (G2) PIN the MEMORY capability row explicitly (parity above is opportunistic — it
            # passes on any frame; here we WAIT for a MEMORY-bearing frame, then require the row).
            mem_seen = False
            deadline = time.time() + 10
            while time.time() < deadline:
                fl = page.evaluate("(window.JarvisTelemetry.getState().latest||{}).flags_list || []")
                if 'MEMORY' in fl:
                    mem_seen = True
                    break
                time.sleep(0.1)
            check(mem_seen, "(G2) live record carries MEMORY (episodic store up) within 10s")
            expect(page.get_by_text('Episodic memory store')).to_be_visible(timeout=10000)
            check(True, "(G2) MEMORY -> 'Episodic memory store' Capabilities row renders")

            # --- System screen: real system fields only (Tier 0 RAM + Tier 1 inference/storage) ---
            page.get_by_title('System', exact=True).click()
            expect(page.get_by_text('RAM available to JARVIS')).to_be_visible(timeout=10000)
            check(True, "System screen renders 'RAM available to JARVIS' (Tier 0 total_ram_mb)")
            expect(page.get_by_text('Workload duty cycle')).to_be_visible(timeout=10000)
            check(True, "System screen renders the inference duty cycle (honest, not a load gauge)")
            sys_text = page.evaluate("() => { const m = document.querySelector('main'); return m ? m.innerText : ''; }")
            check(('ACTIVE' in sys_text) or ('IDLE' in sys_text),
                  "System screen shows the inference state pill (ACTIVE | IDLE)")
            check(page.evaluate("(window.JarvisTelemetry.getState().latest||{}).infer_active") in (0, 1),
                  "System: infer_active is a real 0/1 field on /events")

            # (G1) PIN the System 'Episodic records' VALUE — must equal the live episodic_count on a
            # MEMORY-bearing frame (flag-gated; shows '—' otherwise). TEETH: fails if the episodic_count
            # rendering breaks (wrong value, '—', or removed). Store value + rendered DOM are read in ONE
            # evaluate (atomic vs the page), then polled until they agree on a real MEMORY frame.
            epi_ok = False
            epi_dbg = None
            deadline = time.time() + 12
            while time.time() < deadline:
                snap = page.evaluate(
                    "() => {"
                    " const rec = (window.JarvisTelemetry.getState().latest) || {};"
                    " const flags = rec.flags_list || [];"
                    " const lab = Array.from(document.querySelectorAll('div'))"
                    "   .find(d => d.textContent.trim() === 'Episodic records');"
                    " let rendered = null;"
                    " if (lab && lab.parentElement) {"
                    "   const kids = Array.from(lab.parentElement.children);"
                    "   const i = kids.indexOf(lab);"
                    "   rendered = kids[i + 1] ? kids[i + 1].textContent.trim() : null;"
                    " }"
                    " return { mem: flags.indexOf('MEMORY') >= 0, count: rec.episodic_count, rendered };"
                    "}")
                epi_dbg = snap
                if snap['mem'] and snap['rendered'] not in (None, '—'):
                    try:
                        rendered_num = int(str(snap['rendered']).replace(',', ''))
                    except (ValueError, TypeError):
                        rendered_num = None
                    if rendered_num is not None and rendered_num == snap['count'] and rendered_num > 0:
                        epi_ok = True
                        break
                time.sleep(0.1)
            check(epi_ok,
                  "(G1) System 'Episodic records' renders == live episodic_count (snap=%r)" % (epi_dbg,))

            # (G2/M4) PIN the System 'Context pool' VALUE — must equal the live pool_decisions on a
            # CONTEXT-bearing frame (flag-gated; '—' otherwise). TEETH: fails if the v2 pool rendering
            # breaks (wrong value, '—', or removed). Same atomic store+DOM read + poll-until-agree.
            ctx_ok = False
            ctx_dbg = None
            deadline = time.time() + 12
            while time.time() < deadline:
                snap = page.evaluate(
                    "() => {"
                    " const rec = (window.JarvisTelemetry.getState().latest) || {};"
                    " const flags = rec.flags_list || [];"
                    " const lab = Array.from(document.querySelectorAll('div'))"
                    "   .find(d => d.textContent.trim() === 'Context pool');"
                    " let rendered = null;"
                    " if (lab && lab.parentElement) {"
                    "   const kids = Array.from(lab.parentElement.children);"
                    "   const i = kids.indexOf(lab);"
                    "   rendered = kids[i + 1] ? kids[i + 1].textContent.trim() : null;"
                    " }"
                    " return { ctx: flags.indexOf('CONTEXT') >= 0, dec: rec.pool_decisions,"
                    "          ev: rec.pool_events, rendered };"
                    "}")
                ctx_dbg = snap
                if snap['ctx'] and snap['rendered'] not in (None, '—'):
                    try:
                        rendered_num = int(str(snap['rendered']).replace(',', ''))
                    except (ValueError, TypeError):
                        rendered_num = None
                    if rendered_num is not None and rendered_num == snap['dec'] and rendered_num > 0:
                        ctx_ok = True
                        break
                time.sleep(0.1)
            check(ctx_ok,
                  "(G2/M4) System 'Context pool' renders == live pool_decisions (snap=%r)" % (ctx_dbg,))

            # (G3/M4b) PIN the System 'Preambles packed' VALUE — must equal the live retrieval_hits on
            # a RETRIEVAL-flagged frame (flag-gated; '—' otherwise). The golden 'infer' frame carries
            # hits=3. TEETH: fails if the retrieval rendering breaks (wrong value, '—', or removed).
            # Same atomic store+DOM read + poll-until-agree.
            retr_ok = False
            retr_dbg = None
            deadline = time.time() + 12
            while time.time() < deadline:
                snap = page.evaluate(
                    "() => {"
                    " const rec = (window.JarvisTelemetry.getState().latest) || {};"
                    " const flags = rec.flags_list || [];"
                    " const lab = Array.from(document.querySelectorAll('div'))"
                    "   .find(d => d.textContent.trim() === 'Preambles packed');"
                    " let rendered = null;"
                    " if (lab && lab.parentElement) {"
                    "   const kids = Array.from(lab.parentElement.children);"
                    "   const i = kids.indexOf(lab);"
                    "   rendered = kids[i + 1] ? kids[i + 1].textContent.trim() : null;"
                    " }"
                    " return { retr: flags.indexOf('RETRIEVAL') >= 0, hits: rec.retrieval_hits, rendered };"
                    "}")
                retr_dbg = snap
                if snap['retr'] and snap['rendered'] not in (None, '—'):
                    try:
                        rendered_num = int(str(snap['rendered']).replace(',', ''))
                    except (ValueError, TypeError):
                        rendered_num = None
                    if rendered_num is not None and rendered_num == snap['hits'] and rendered_num > 0:
                        retr_ok = True
                        break
                time.sleep(0.1)
            check(retr_ok,
                  "(G3/M4b) System 'Preambles packed' renders == live retrieval_hits (snap=%r)" % (retr_dbg,))

            # (#6/M2) PIN the System 'Patterns promoted' VALUE — must equal the live
            # cache_growth_count on a CACHE_GROWTH-flagged frame (flag-gated; '—' otherwise). The
            # golden 'infer' frame carries cache_growth_count=12. TEETH: fails if the cache-growth
            # rendering breaks (wrong value, '—', or removed). Same atomic store+DOM read + poll.
            cg_ok = False
            cg_dbg = None
            deadline = time.time() + 12
            while time.time() < deadline:
                snap = page.evaluate(
                    "() => {"
                    " const rec = (window.JarvisTelemetry.getState().latest) || {};"
                    " const flags = rec.flags_list || [];"
                    " const lab = Array.from(document.querySelectorAll('div'))"
                    "   .find(d => d.textContent.trim() === 'Patterns promoted');"
                    " let rendered = null;"
                    " if (lab && lab.parentElement) {"
                    "   const kids = Array.from(lab.parentElement.children);"
                    "   const i = kids.indexOf(lab);"
                    "   rendered = kids[i + 1] ? kids[i + 1].textContent.trim() : null;"
                    " }"
                    " return { cg: flags.indexOf('CACHE_GROWTH') >= 0, count: rec.cache_growth_count, rendered };"
                    "}")
                cg_dbg = snap
                if snap['cg'] and snap['rendered'] not in (None, '—'):
                    try:
                        rendered_num = int(str(snap['rendered']).replace(',', ''))
                    except (ValueError, TypeError):
                        rendered_num = None
                    if rendered_num is not None and rendered_num == snap['count'] and rendered_num > 0:
                        cg_ok = True
                        break
                time.sleep(0.1)
            check(cg_ok,
                  "(#6/M2) System 'Patterns promoted' renders == live cache_growth_count (snap=%r)" % (cg_dbg,))

            # (#4/M2) PIN the System 'Distilled facts' VALUE — must equal the live
            # semantic_fact_count on a SEMANTIC-flagged frame (flag-gated; '—' otherwise). The
            # golden 'infer' frame carries 1 (the honest ~1-fact box yield). TEETH: fails if the
            # semantic rendering breaks (wrong value, '—', or removed). Still on the System screen.
            sem_ok = False
            sem_dbg = None
            deadline = time.time() + 12
            while time.time() < deadline:
                snap = page.evaluate(
                    "() => {"
                    " const rec = (window.JarvisTelemetry.getState().latest) || {};"
                    " const flags = rec.flags_list || [];"
                    " const lab = Array.from(document.querySelectorAll('div'))"
                    "   .find(d => d.textContent.trim() === 'Distilled facts');"
                    " let rendered = null;"
                    " if (lab && lab.parentElement) {"
                    "   const kids = Array.from(lab.parentElement.children);"
                    "   const i = kids.indexOf(lab);"
                    "   rendered = kids[i + 1] ? kids[i + 1].textContent.trim() : null;"
                    " }"
                    " return { sem: flags.indexOf('SEMANTIC') >= 0, count: rec.semantic_fact_count, rendered };"
                    "}")
                sem_dbg = snap
                if snap['sem'] and snap['rendered'] not in (None, '—'):
                    try:
                        rendered_num = int(str(snap['rendered']).replace(',', ''))
                    except (ValueError, TypeError):
                        rendered_num = None
                    if rendered_num is not None and rendered_num == snap['count'] and rendered_num > 0:
                        sem_ok = True
                        break
                time.sleep(0.1)
            check(sem_ok,
                  "(#4/M2) System 'Distilled facts' renders == live semantic_fact_count (snap=%r)" % (sem_dbg,))

            # (K/M3) PIN the System 'PB restarts (self-heal)' VALUE — must equal the live
            # restart_count on an ACTIONS-flagged frame (flag-gated; '—' otherwise). The golden
            # 'infer' frame carries restart_count=3. TEETH: fails if the self-heal rendering breaks
            # (wrong value, '—', or removed). Still on the System screen.
            act_ok = False
            act_dbg = None
            deadline = time.time() + 12
            while time.time() < deadline:
                snap = page.evaluate(
                    "() => {"
                    " const rec = (window.JarvisTelemetry.getState().latest) || {};"
                    " const flags = rec.flags_list || [];"
                    " const lab = Array.from(document.querySelectorAll('div'))"
                    "   .find(d => d.textContent.trim() === 'PB restarts (self-heal)');"
                    " let rendered = null;"
                    " if (lab && lab.parentElement) {"
                    "   const kids = Array.from(lab.parentElement.children);"
                    "   const i = kids.indexOf(lab);"
                    "   rendered = kids[i + 1] ? kids[i + 1].textContent.trim() : null;"
                    " }"
                    " return { act: flags.indexOf('ACTIONS') >= 0, count: rec.restart_count, rendered };"
                    "}")
                act_dbg = snap
                if snap['act'] and snap['rendered'] not in (None, '—'):
                    try:
                        rendered_num = int(str(snap['rendered']).replace(',', ''))
                    except (ValueError, TypeError):
                        rendered_num = None
                    if rendered_num is not None and rendered_num == snap['count'] and rendered_num > 0:
                        act_ok = True
                        break
                time.sleep(0.1)
            check(act_ok,
                  "(K/M3) System 'PB restarts (self-heal)' renders == live restart_count (snap=%r)" % (act_dbg,))

            # (#5/M2) PIN the SHIELD 'Actions with learned risk' VALUE — must equal the live
            # shield_learn_keys on a SHIELD_LEARN-flagged frame (flag-gated; '—' otherwise). The
            # golden 'infer' frame carries shield_learn_keys=1 (the D-d probe shape). MONITOR-ONLY:
            # the section must also carry the 'not a live blocker' wording. TEETH: fails if the
            # failure-learning rendering breaks (wrong value, '—', or removed).
            page.get_by_title('SHIELD', exact=True).click()
            expect(page.get_by_text('Failure-learning')).to_be_visible(timeout=10000)
            sl_ok = False
            sl_dbg = None
            deadline = time.time() + 12
            while time.time() < deadline:
                snap = page.evaluate(
                    "() => {"
                    " const rec = (window.JarvisTelemetry.getState().latest) || {};"
                    " const flags = rec.flags_list || [];"
                    " const lab = Array.from(document.querySelectorAll('div'))"
                    "   .find(d => d.textContent.trim() === 'Actions with learned risk');"
                    " let rendered = null;"
                    " if (lab && lab.parentElement) {"
                    "   const kids = Array.from(lab.parentElement.children);"
                    "   const i = kids.indexOf(lab);"
                    "   rendered = kids[i + 1] ? kids[i + 1].textContent.trim() : null;"
                    " }"
                    " const m = document.querySelector('main');"
                    " return { sl: flags.indexOf('SHIELD_LEARN') >= 0, keys: rec.shield_learn_keys,"
                    "          rendered, monitor: m ? m.innerText.indexOf('not a live blocker') >= 0 : false };"
                    "}")
                sl_dbg = snap
                if snap['sl'] and snap['monitor'] and snap['rendered'] not in (None, '—'):
                    try:
                        rendered_num = int(str(snap['rendered']).replace(',', ''))
                    except (ValueError, TypeError):
                        rendered_num = None
                    if rendered_num is not None and rendered_num == snap['keys'] and rendered_num > 0:
                        sl_ok = True
                        break
                time.sleep(0.1)
            check(sl_ok,
                  "(#5/M2) SHIELD 'Actions with learned risk' renders == live shield_learn_keys, "
                  "monitor-only worded (snap=%r)" % (sl_dbg,))

            # (v4) PIN the CommandCenter Throughput VALUE — must equal infer_last_tok_x100/100
            # on a frame carrying the measured field (the golden 'infer' frame carries 152 ->
            # '1.52'), captured by the early watcher installed at mount (the frame is live for
            # only ~1 s). TEETH: fails if the live tok/s rendering breaks or regresses to the
            # static 5.46 placeholder.
            tok_ok = False
            tok_dbg = None
            deadline = time.time() + 12
            while time.time() < deadline:
                snap = page.evaluate("() => window.__tokPin")
                tok_dbg = snap
                if snap and snap.get('seen'):
                    tok_ok = True
                    break
                time.sleep(0.1)
            page.evaluate("() => { if (window.__tokPinTimer) clearInterval(window.__tokPinTimer); }")
            check(tok_ok,
                  "(v4) Throughput rendered infer_last_tok_x100/100 as the live tok/s (watcher=%r)" % (tok_dbg,))

            check(errors == [], "no console errors / pageerrors (saw %d)" % len(errors))
            for e in errors[:10]:
                print("    ERR:", e)
            page.close()

            # FALLBACK PHASE: static server (no /events) -> SIMULATED badge appears
            static = subprocess.Popen(
                [sys.executable, '-m', 'http.server', str(STATIC_PORT), '--directory',
                 os.path.join(REPO, 'phase4', 'console')],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            wait_port('http://127.0.0.1:%d/' % STATIC_PORT)
            page2 = browser.new_page()
            page2.goto('http://127.0.0.1:%d/' % STATIC_PORT)
            expect(page2.get_by_text('functional-unverified')).to_be_visible(timeout=15000)
            expect(page2.get_by_text('SIMULATED · replay')).to_be_visible(timeout=15000)
            check(page2.evaluate("window.JarvisTelemetry.getState().simulated") is True,
                  "fallback: simulated == True when /events is absent (badge appears)")
            page2.close()

            browser.close()
    finally:
        bridge.terminate()
        try:
            bridge.wait(timeout=5)
        except Exception:
            bridge.kill()
        if static is not None:
            static.terminate()
            try:
                static.wait(timeout=5)
            except Exception:
                static.kill()

    print("\n== Results: %d PASS, %d FAIL ==" % (_P, _F))
    return 1 if _F else 0


if __name__ == '__main__':
    sys.exit(main())
