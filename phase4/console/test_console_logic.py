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
    page.clock.run_for(10)                   # fire the stub onopen (setTimeout(0))
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
