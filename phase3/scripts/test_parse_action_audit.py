#!/usr/bin/env python3
"""C->Python round-trip for the action-audit store (Phase 6 K/M0).

The C test (test_action_audit.c --dump <path>) writes a FIXED known fixture to
a raw region file; this parses it with parse_action_audit.py and asserts every
field exact — pinning the 512-byte action_audit_rec_t on-disk layout across C
and Python (a layout shift fails the field checks; a header-checksum change
fails checksum_ok). Mirrors test_parse_episodic.py, teeth-proven the same way.

Usage: python3 test_parse_action_audit.py <fixture.bin>
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parse_action_audit  # noqa: E402

ACT_AUDIT_MAGIC = 0x4A414354

# MUST match the fixture written by test_action_audit.c dump_fixture() exactly.
# (t_ms, action_id, trust, trust_name, verdict, verdict_name, risk_x100,
#  outcome, outcome_name, trigger)
FIXTURE = [
    (100, 1,      1, 'NOTIFY',  0, 'EXECUTED', 10,  0, 'OK',   "PB heartbeat age 12000 ms"),
    (200, 2,      0, 'AUTO',    0, 'EXECUTED', 0,   0, 'OK',   "q_errors delta 3"),
    (300, 0xFFFE, 3, 'REQUIRE', 1, 'BLOCKED',  100, 2, 'NA',   "induced block probe"),
    (400, 1,      2, 'REQUEST', 2, 'PROPOSED', 60,  1, 'FAIL', ""),
]

_passed = 0
_failed = 0


def check(cond, msg):
    global _passed, _failed
    if cond:
        _passed += 1
    else:
        _failed += 1
        print(f"  FAIL: {msg}")


def main():
    if len(sys.argv) < 2:
        print("usage: test_parse_action_audit.py <fixture.bin>")
        sys.exit(2)
    with open(sys.argv[1], 'rb') as f:
        data = f.read()

    hdr = parse_action_audit.parse_header(data)
    check(hdr['magic'] == ACT_AUDIT_MAGIC, f"header magic == JACT (got 0x{hdr['magic']:08X})")
    check(hdr['checksum_ok'], "header XOR checksum OK")
    check(hdr['boot_id'] == 1, f"boot_id == 1 (got {hdr['boot_id']})")
    check(hdr['total'] == len(FIXTURE), f"total == {len(FIXTURE)} (got {hdr['total']})")
    check(hdr['cursor'] == len(FIXTURE), "cursor == record count (not wrapped)")

    recs = parse_action_audit.iter_records(data)
    check(len(recs) == len(FIXTURE), f"{len(FIXTURE)} records decoded (got {len(recs)})")

    for i, (t_ms, aid, trust, trust_name, verdict, verdict_name,
            risk, outcome, outcome_name, trigger) in enumerate(FIXTURE):
        if i >= len(recs):
            check(False, f"rec{i} missing")
            continue
        r = recs[i]
        check(r['boot_id'] == 1, f"rec{i} boot_id == 1")
        check(r['seq'] == i, f"rec{i} seq == {i} (got {r['seq']})")
        check(r['t_ms'] == t_ms, f"rec{i} t_ms == {t_ms} (got {r['t_ms']})")
        check(r['action_id'] == aid, f"rec{i} action_id == {aid} (got {r['action_id']})")
        check(r['trust_level'] == trust and r['trust_name'] == trust_name,
              f"rec{i} trust {trust}/{trust_name} (got {r['trust_level']}/{r['trust_name']})")
        check(r['verdict'] == verdict and r['verdict_name'] == verdict_name,
              f"rec{i} verdict {verdict}/{verdict_name} (got {r['verdict']}/{r['verdict_name']})")
        check(r['risk_x100'] == risk, f"rec{i} risk == {risk} (got {r['risk_x100']})")
        check(r['outcome'] == outcome and r['outcome_name'] == outcome_name,
              f"rec{i} outcome {outcome}/{outcome_name} (got {r['outcome']}/{r['outcome_name']})")
        check(r['trigger_len'] == len(trigger), f"rec{i} trigger_len == {len(trigger)}")
        check(r['trigger'] == trigger, f"rec{i} trigger exact (got {r['trigger']!r})")

    print(f"\n== {_passed} PASS, {_failed} FAIL ==")
    sys.exit(1 if _failed else 0)


if __name__ == '__main__':
    main()
