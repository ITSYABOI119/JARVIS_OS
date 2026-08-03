#!/usr/bin/env python3
"""A6a -- C -> Python round-trip for the NVMe telemetry log.

nvme_log.py was the only one of the three store parsers with no round-trip
(parse_episodic and parse_action_audit both have one). The parser is the ONLY way
the box's durable log is read off-hardware, so a decode bug there is invisible until
someone is trying to diagnose a boot.

The fixtures are written by `test_nvme_log --dump <case> <path>`, which drives the
REAL nvme_log.c through its mock disk. So this asserts against bytes the shipping
writer produced, not against a hand-rolled idea of the format.

THE SUBTLE CASE, and why two fixtures are full-size: parse_nvme_log derives its wrap
modulus from the supplied buffer (`slots = len(data)//512 - 1`), not from
NVME_LOG_MAX_ENTRIES. A truncated wrapped capture therefore rotates by a different
modulus than nvme_log.c used when writing -- both sides self-consistent, both wrong.
`wrapped` and `migrated` are dumped at the full 2701 sectors so the two moduli agree.

Usage: python3 test_parse_nvme_log.py <plain.bin> <wrapped.bin> <migrated.bin>
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parse_nvme_log as P    # noqa: E402

MAX = 2700          # NVME_LOG_MAX_ENTRIES (phase3/src/drivers/nvme_log.h:29)

_failed = 0


def check(cond, msg):
    global _failed
    if cond:
        print("  PASS: %s" % msg)
    else:
        _failed += 1
        print("  FAIL: %s" % msg)


def load(path):
    with open(path, "rb") as f:
        return f.read()


def case_plain(path):
    print("\n-- plain (non-wrapped, 5 entries) --")
    data = load(path)
    h = P.parse_header(data)
    check(h["magic"] == P.MAGIC, "magic is JRLG")
    check(h["checksum_ok"], "header checksum verifies")
    check(h["boot_id"] == 1, "boot_id == 1 (zeroed region -> fresh header)")
    check(h["cursor"] == 5, "cursor == 5 after 5 writes")
    check(h["total_entries"] == 5, "total_entries == 5")
    check(h["wrapped"] is False, "not wrapped (total <= slots)")

    recs = list(P.iter_records(data))
    check(len(recs) == 5, "5 records decoded")
    # Types were written as LOG_BOOT + i, i.e. 1..5.
    want = [(0, 1, "BOOT", "plain-entry-0"),
            (1, 2, "SELFTEST", "plain-entry-1"),
            (2, 3, "MODEL_LOAD", "plain-entry-2"),
            (3, 4, "INFER", "plain-entry-3"),
            (4, 5, "IPC_STATS", "plain-entry-4")]
    ok = True
    for r, (seq, tp, tname, payload) in zip(recs, want):
        if not (r["seq"] == seq and r["type"] == tp and
                r["type_name"] == tname and r["payload"] == payload and
                r["boot_id"] == 1):
            ok = False
            print("      got %r" % r)
    check(ok, "all 5 records field-exact (boot_id, seq, type, type_name, payload)")
    check([r["slot"] for r in recs] == [0, 1, 2, 3, 4], "slot order is 0..4 (oldest -> newest)")


def case_wrapped(path):
    print("\n-- wrapped (MAX+5 writes) --")
    data = load(path)
    h = P.parse_header(data)
    check(h["checksum_ok"], "header checksum verifies")
    check(h["total_entries"] == MAX + 5, "total_entries == 2705 (monotonic lifetime count)")
    check(h["cursor"] == 5, "cursor == 2705 mod 2700 == 5")
    check(h["slots"] == MAX, "full-size capture -> slots == NVME_LOG_MAX_ENTRIES")
    check(h["wrapped"] is True, "wrapped (total > slots)")

    recs = list(P.iter_records(data))
    check(len(recs) == MAX, "2700 records decoded (the buffer holds the latest 2700)")

    # THE ORDERING ASSERTION. Entry i lands in slot i % 2700, so slot s holds the most
    # recent writer of that slot. Reading from cursor gives oldest -> newest, which
    # means the first record is w5 and the last is w2704.
    check(recs[0]["payload"] == "w5" and recs[0]["seq"] == 5,
          "oldest record is w5 (slot == cursor == 5)")
    check(recs[-1]["payload"] == "w2704" and recs[-1]["seq"] == MAX + 4,
          "newest record is w2704 (slot 4, the last one written)")
    check([r["payload"] for r in recs[-5:]] ==
          ["w2700", "w2701", "w2702", "w2703", "w2704"],
          "the newest 5 are the 5 that wrapped past the end")

    # Monotonic seq across the whole returned order is the strongest ordering check:
    # any rotation error breaks it somewhere.
    seqs = [r["seq"] for r in recs]
    check(seqs == sorted(seqs), "seq is monotonically increasing across all 2700 (true oldest -> newest)")
    check(seqs[0] == 5 and seqs[-1] == MAX + 4, "seq spans 5..2704")
    check(all(r["boot_id"] == 1 for r in recs), "every record carries boot_id 1")


def case_migrated(path):
    print("\n-- migrated (stale pre-wrap cursor == MAX, clamped on init) --")
    data = load(path)
    h = P.parse_header(data)
    check(h["checksum_ok"], "header checksum verifies")
    check(h["boot_id"] == 6, "boot_id 5 -> 6 (a VALID stale header increments, not resets)")
    check(h["cursor"] == 3, "cursor clamped MAX -> 0, then 3 writes -> cursor 3")
    check(h["total_entries"] == MAX + 3, "total_entries continues from MAX (2703)")

    recs = list(P.iter_records(data))
    check(len(recs) == MAX, "2700 slots decoded")

    # The clamp is what puts the post-migration writes in slots 0,1,2; reading from
    # cursor 3 therefore returns them LAST. Everything else is the zeroed region.
    live = [r for r in recs if r["boot_id"] == 6]
    check(len(live) == 3, "exactly 3 records carry the post-migration boot_id 6")
    check([r["payload"] for r in live] ==
          ["post-migration-0", "post-migration-1", "post-migration-2"],
          "the 3 post-migration payloads decode in order")
    check([r["slot"] for r in live] == [0, 1, 2],
          "they occupy slots 0,1,2 -- proving the stale cursor was clamped to 0")
    check([r["seq"] for r in live] == [MAX, MAX + 1, MAX + 2],
          "their seq continues the lifetime count (2700,2701,2702)")
    check(recs[-3:] == live, "wrap order puts them LAST (newest), read from cursor 3")
    check(all(r["type_name"] == "ERROR" for r in live), "type round-trips as ERROR")


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    print("== test_parse_nvme_log (C -> Python round-trip) ==")
    case_plain(sys.argv[1])
    case_wrapped(sys.argv[2])
    case_migrated(sys.argv[3])
    print("\n== %s ==" % ("ALL PASS" if _failed == 0 else "%d FAILED" % _failed))
    return 1 if _failed else 0


if __name__ == "__main__":
    sys.exit(main())
