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
]

# (b) Honest-framing markers that MUST stay present somewhere in the console
#     (case-insensitive). Proves the honest scaffolding wasn't stripped.
REQUIRED = [
    "not a blocker",
    "ALLOW",
    "no queries yet",
    "collecting",
    "SIMULATED",
    "truncated tail",
    "5.46",
    "not the deployed build",
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

    # special case: "19.7" (the bench-off native-engine figure) may appear ONLY
    # if the "not the deployed build" caveat is also present.
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

    print("\n== Results: %d PASS, %d FAIL ==" % (_PASS, _FAIL))
    return 1 if _FAIL else 0


if __name__ == '__main__':
    sys.exit(main())
