#!/usr/bin/env python3
"""
test_console_logic.py - Layer B: telemetry.js logic in headless Chromium.

Drives the REAL telemetry.js with a deterministic page.clock + a stubbed
EventSource (NO real SSE), asserting connState live/stale/crcfail, seq-gap
droppedPackets, new-boot reset, and cold-start queriesPerSec() purely via
window.JarvisTelemetry.getState(). Zero console errors.

The 2500 / 1500 ms clock steps bracket telemetry.js STALE_MS = 2800 (2500 < 2800
< 4000) — a deliberate watchdog change that crosses those bounds fails loudly.

Run: python3 phase4/console/test_console_logic.py   (nonzero exit on FAIL)
Requires: pip install playwright==1.60.0 ; playwright install --only-shell chromium
"""

import os
import subprocess
import sys
import time
import urllib.request

from playwright.sync_api import sync_playwright, expect

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
PORT = 8139
BASE = 'http://127.0.0.1:%d/' % PORT

# Stub EventSource installed BEFORE page scripts run: stores the instance on
# window.__es and exposes window.__push(rec) to feed a record into onmessage.
# onopen fires via a MICROTASK (not setTimeout) so it runs right after
# telemetry.js assigns es.onopen and BEFORE the faked-clock 1500ms failTimer —
# otherwise the failTimer wins and telemetry.js falls back to its simulator,
# which keeps pushing fresh records and the stale-watchdog never trips. (This
# was a real CI-vs-local flake: setTimeout(0) onopen lost the race on CI.)
STUB = r"""
window.__esCount = 0;
class StubES {
  constructor(url) {
    this.url = url; this.onopen = null; this.onmessage = null; this.onerror = null;
    window.__es = this; window.__esCount++;
    Promise.resolve().then(() => { if (this.onopen) this.onopen(); });
  }
  close() {}
}
window.EventSource = StubES;
window.__push = function (rec) {
  if (window.__es && window.__es.onmessage) window.__es.onmessage({ data: JSON.stringify(rec) });
};
"""

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


def start_server():
    p = subprocess.Popen(
        [sys.executable, '-m', 'http.server', str(PORT), '--directory',
         os.path.join(REPO, 'phase4', 'console')],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(80):
        try:
            urllib.request.urlopen(BASE, timeout=1)
            return p
        except Exception:
            time.sleep(0.1)
    p.kill()
    raise RuntimeError("http.server did not come up on %s" % BASE)


def state(page):
    return page.evaluate("window.JarvisTelemetry.getState()")


def fresh_page(browser, errors):
    """A new page with the error listeners, faked clock, and stubbed EventSource
    in place, mounted and connected (onopen fired)."""
    page = browser.new_page()
    page.on('console', lambda m: errors.append('console: ' + m.text) if m.type == 'error' else None)
    page.on('pageerror', lambda e: errors.append('pageerror: ' + str(e)))
    page.clock.install(time=0)               # before goto -> the load-time watchdog is faked
    page.add_init_script(STUB)               # before goto -> EventSource is stubbed
    page.goto(BASE)
    expect(page.get_by_text('functional-unverified')).to_be_visible(timeout=15000)
    page.evaluate("window.JarvisTelemetry.connect('/events')")
    check(bool(page.evaluate("!!window.__es")), "EventSource stub wired (window.__es set)")
    page.clock.run_for(10)                   # flush timers; the stub onopen fired via microtask
    return page


def main():
    server = start_server()
    errors = []
    try:
        with sync_playwright() as pw:
            browser = pw.chromium.launch()

            # LIVE + STALE (steps bracket the STALE_MS = 2800 watchdog: 2500 < 2800 < 4000)
            page = fresh_page(browser, errors)
            page.evaluate("window.__push({crc_ok:true, boot_id:1, seq:1, q_total:0, recv_ts:0})")
            check(state(page)['connState'] == 'live', "connState 'live' after a CRC-valid packet")
            check(state(page)['simulated'] is False, "on the stubbed stream, NOT the simulator fallback")
            page.clock.run_for(2500)
            check(state(page)['connState'] == 'live', "still 'live' at 2500ms (< STALE_MS 2800)")
            page.clock.run_for(1500)
            check(state(page)['connState'] == 'stale', "'stale' at 4000ms (> STALE_MS 2800)")
            page.close()

            # CRCFAIL
            page = fresh_page(browser, errors)
            page.evaluate("window.__push({crc_ok:false, boot_id:1, seq:1, q_total:0, recv_ts:0})")
            check(state(page)['connState'] == 'crcfail', "connState 'crcfail' on crc_ok:false")
            page.close()

            # SEQ-GAP + NEW-BOOT RESET
            page = fresh_page(browser, errors)
            page.evaluate("window.__push({crc_ok:true, boot_id:1, seq:3, q_total:10, recv_ts:0})")
            page.evaluate("window.__push({crc_ok:true, boot_id:1, seq:6, q_total:20, recv_ts:1})")
            check(state(page)['droppedPackets'] == 2, "droppedPackets == 2 after seq 3 -> 6")
            page.evaluate("window.__push({crc_ok:true, boot_id:2, seq:1, q_total:30, recv_ts:2})")
            check(state(page)['droppedPackets'] == 0, "droppedPackets reset to 0 on a new boot_id")
            page.close()

            # COLD-START queriesPerSec()
            page = fresh_page(browser, errors)
            page.evaluate("window.__push({crc_ok:true, boot_id:1, seq:1, q_total:0, recv_ts:100})")
            check(page.evaluate("window.JarvisTelemetry.queriesPerSec()") is None,
                  "queriesPerSec() is None with < 2 samples")
            page.evaluate("window.__push({crc_ok:true, boot_id:1, seq:2, q_total:10, recv_ts:101})")
            qps = page.evaluate("window.JarvisTelemetry.queriesPerSec()")
            check(qps is not None and qps > 0, "queriesPerSec() > 0 after 2 samples 1s apart (got %r)" % qps)
            page.close()

            # CONTROL-IN REPLY ROUTING (6-5/M3-4b) ---------------------------------------
            # A control_reply record rides the SAME SSE stream but is NOT a telemetry
            # packet: it carries the STRING kind 'control_reply' and its seq is the
            # control-IN request sequence, an unrelated counter. It must NEVER become
            # state.latest, never touch connState, and never enter the seq-gap accounting.
            # TEETH: the reply seq (12345) is far ahead of the packet seq (5), so if it
            # leaked into ingest() the droppedPackets assertions below explode by ~12k.
            page = fresh_page(browser, errors)
            page.evaluate("window.__push({crc_ok:true, boot_id:1, seq:5, q_total:10, recv_ts:0})")
            before = state(page)
            check(before['connState'] == 'live' and before['droppedPackets'] == 0,
                  "baseline: live telemetry packet ingested, no drops")

            page.evaluate("window.__push({kind:'control_reply', verdict:0, verdict_name:'answered',"
                          " seq:12345, text:'a page fault traps to the kernel', crc_ok:true, recv_ts:1})")
            after = state(page)
            latest = after['latest'] or {}
            check(latest.get('seq') == 5 and latest.get('q_total') == 10,
                  "control_reply did NOT become state.latest (the telemetry packet is still latest)")
            check(after['connState'] == 'live',
                  "control_reply left connState untouched (still 'live', got %r)" % after['connState'])
            check(after['droppedPackets'] == 0,
                  "control_reply left droppedPackets untouched (0, got %r)" % after['droppedPackets'])

            # the NEXT real packet must still line up against the last PACKET seq (5 -> 6),
            # proving the reply never clobbered lastSeq.
            page.evaluate("window.__push({crc_ok:true, boot_id:1, seq:6, q_total:20, recv_ts:2})")
            check(state(page)['droppedPackets'] == 0,
                  "seq accounting intact after a reply (packet 5 -> 6 shows no gap)")

            # ...and the reply IS exposed, in its own ring, with the right fields.
            reps = state(page)['replies']
            check(isinstance(reps, list) and len(reps) == 1,
                  "reply landed in the replies ring (len == 1, got %r)" % (len(reps) if isinstance(reps, list) else reps))
            r0 = reps[0] if reps else {}
            check(r0.get('verdict') == 0 and r0.get('seq') == 12345
                  and r0.get('text') == 'a page fault traps to the kernel' and r0.get('crc_ok') is True,
                  "answered reply exposed with the right verdict/seq/text/crc (got %r)" % (r0,))

            # a REFUSED reply is exposed too (the console renders it as an abuse-class refusal,
            # never as an answer) — both verdicts must survive the ring.
            page.evaluate("window.__push({kind:'control_reply', verdict:1, verdict_name:'refused',"
                          " seq:12346, text:'refuse key-extraction', crc_ok:true, recv_ts:3})")
            reps = state(page)['replies']
            check(len(reps) == 2, "refused reply appended (len == 2, got %r)" % len(reps))
            r1 = reps[1] if len(reps) > 1 else {}
            check(r1.get('verdict') == 1 and r1.get('verdict_name') == 'refused'
                  and r1.get('seq') == 12346 and r1.get('text') == 'refuse key-extraction',
                  "refused reply exposed with its reason label (got %r)" % (r1,))
            # #9: a TRUNCATED reply (verdict 4) must survive the ring carrying its TEXT. The ring
            # is verdict-agnostic by design, so this pins that nothing added a verdict filter —
            # a truncated answer is a real answer and dropping it here would lose it silently.
            page.evaluate("window.__push({kind:'control_reply', verdict:4,"
                          " verdict_name:'answered (truncated)', seq:12347,"
                          " text:'a page fault occurs when', crc_ok:true, recv_ts:5})")
            reps = state(page)['replies']
            check(len(reps) == 3, "truncated reply appended (len == 3, got %r)" % len(reps))
            r2 = reps[2] if len(reps) > 2 else {}
            check(r2.get('verdict') == 4 and r2.get('seq') == 12347
                  and r2.get('text') == 'a page fault occurs when',
                  "#9: verdict-4 reply survives the ring WITH its answer text (got %r)" % (r2,))
            check((state(page)['latest'] or {}).get('seq') == 6,
                  "after three replies, state.latest is STILL the last telemetry packet")
            page.close()

            browser.close()

        check(errors == [], "no console errors / pageerrors (saw %d)" % len(errors))
        for e in errors[:10]:
            print("    ERR:", e)
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except Exception:
            server.kill()

    print("\n== Results: %d PASS, %d FAIL ==" % (_P, _F))
    return 1 if _F else 0


if __name__ == '__main__':
    sys.exit(main())
