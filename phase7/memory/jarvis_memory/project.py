"""The JSEM projection (Phase 7 MS3a): the household's current beliefs, offline, in the box's format.

The box has a semantic-fact region, `JSEM`, at LBA 21,110,000: a 512-byte header sector and 4096
512-byte `semantic_fact_t` slots (phase3/src/ai/semantic_store.h). This module renders the Main-PC
store's current household beliefs into that exact byte image. Nothing here touches the box: the image
is a file, and writing it to the device is MS3b, the operator's own act.

The rules are the design's §9 as pre-registered on 2026-09-25 (PHASE_7_MEMORY_DESIGN.md, the MS3
block), which supersede §9's original list where they differ:

- SELECTION is the household: current rows whose subject is a household person (one some
  `cluster.person_id` names), or a household or topic subject; edges whose two ends are household
  persons; preferences of household persons. A stated row is always taken, an inferred one at
  confidence >= SURFACE_THRESHOLD. Facts, then edges, then preferences, each by row id. More than
  4096 rows are refused, never cut.
- THE KEY is FNV-1a 64 over a canonical string per table, because `sem_store_upsert` matches on
  `key` alone and a predicate-and-subject key would merge rows that must stay apart.
- THE BUILDER AUTHORS THE HEADER, or `sem_store_init` would treat the region as absent.
- THE TEXT follows the store's own full-text rendering, plus a (source_kind, n days) suffix.

Any row that breaks a rule refuses the whole build with `ProjectionRefused`, whose message names the
rule. A projection is all or nothing: a partial image would be a silent cut.

Standard library only.
"""
import calendar
import hashlib
import math
import os
import sqlite3
import struct
import urllib.parse
from datetime import datetime
from pathlib import Path

from .confidence import SURFACE_THRESHOLD, distinct_days
from .registry import predicate_words

SECTOR = 512
MAX_FACTS = 4096
MAGIC = 0x4A53454D                      # "JSEM"
VERSION = 1
FACT_PROFILE = 2                        # SEM_FACT_PROFILE
TEXT_MAX = 440                          # SEM_FACT_TEXT_MAX
REGION_BYTES = (MAX_FACTS + 1) * SECTOR  # 2,097,664: the header sector plus 4096 slots
FNV_BASIS = 0xcbf29ce484222325          # cache_hash's basis
FNV_PRIME = 0x100000001b3               # cache_hash's prime
SURFACE = SURFACE_THRESHOLD             # imported, never retyped

REC_FMT = "<IIQQHHHH"                   # boot_id, seq, t_ms, key, fact_type, support, conf, text_len
REPO_ROOT = Path(__file__).resolve().parents[3]

# The refusal rules, in the order they are checked. Each message starts with its rule's name, so a
# test can assert WHICH rule fired rather than only that one did.
RULE_TOO_MANY = "more than 4096 selected rows"
RULE_NO_SPAN = "a selected row with no supporting span"
RULE_NOT_PRINTABLE = "a byte outside 0x20-0x7E in its key string or its text"
RULE_TOO_LONG = "a text over 440 bytes"
RULE_DUPLICATE_KEY = "a key equal to one already built"
RULE_INSIDE_REPO = "an image path inside the repository"
# MS3b-1: three more, so that no input escapes as a raw traceback. The two time rules run per row,
# after the no-span rule and before the printable-ASCII rule; the UNC rule runs before the store is
# opened; the manifest-directory rule is the CLI's, checked before any image is written.
RULE_BAD_TIME = "a supporting span whose said_at is not an ISO timestamp"
RULE_PRE_EPOCH = "a supporting span dated before 1970"
RULE_UNC_PATH = "a store path on a network share (copy it local first)"
RULE_NO_MANIFEST_DIR = "a manifest path whose directory does not exist"

_SPAN_LINK = {
    "fact": ("fact_span", "fact_id"),
    "edge": ("edge_span", "edge_id"),
    "preference": ("preference_span", "preference_id"),
}


class ProjectionRefused(Exception):
    """A rule of the projection was broken; the message names the rule."""


def fnv1a64(data: bytes) -> int:
    h = FNV_BASIS
    for b in data:
        h ^= b
        h = (h * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return h


def _printable(s: str) -> bool:
    return all(0x20 <= ord(c) <= 0x7E for c in s)


def _s(v) -> str:
    return "" if v is None else str(v)


def _connect_ro(db_path):
    # READ-ONLY by construction, so a projection can never change a store. A missing file raises
    # sqlite3.OperationalError here rather than being created empty.
    uri = "file:%s?mode=ro" % urllib.parse.quote(Path(db_path).resolve().as_posix(), safe="/:")
    conn = sqlite3.connect(uri, uri=True)
    conn.row_factory = sqlite3.Row
    return conn


def _select(conn):
    """The household's projectable rows: [(table, row)], facts then edges then preferences."""
    persons = {r[0] for r in conn.execute(
        "select distinct person_id from cluster where person_id is not null")}

    def keep(r):
        return _s(r["source_kind"]).startswith("stated") or r["confidence"] >= SURFACE

    facts = [r for r in conn.execute(
        "select id, subject_kind, subject_id, predicate_id, object_text, object_norm, source_kind, "
        "confidence from fact where valid_to is null order by id")
        if ((r["subject_kind"] == "person" and r["subject_id"] in persons)
            or r["subject_kind"] in ("household", "topic")) and keep(r)]
    edges = [r for r in conn.execute(
        "select id, from_person, to_person, relation_id, source_kind, confidence from edge "
        "where valid_to is null order by id")
        if r["from_person"] in persons and r["to_person"] in persons and keep(r)]
    prefs = [r for r in conn.execute(
        "select id, person_id, topic_norm, polarity, source_kind, confidence from preference "
        "where valid_to is null order by id")
        if r["person_id"] in persons and keep(r)]
    return ([("fact", r) for r in facts] + [("edge", r) for r in edges]
            + [("preference", r) for r in prefs])


def _person(conn, person_id) -> str:
    r = conn.execute("select display_name from person where id=?", (person_id,)).fetchone()
    return r[0] if r and r[0] else "person %d" % person_id


def _key_string(table, r) -> str:
    if table == "fact":
        sid = "" if r["subject_kind"] in ("household", "topic") else _s(r["subject_id"])
        return "prof|fact|%s:%s|%s|%s" % (r["subject_kind"], sid, r["predicate_id"], _s(r["object_norm"]))
    if table == "edge":
        return "prof|edge|%d|%d|%s" % (r["from_person"], r["to_person"], r["relation_id"])
    return "prof|pref|%d|%s|%s" % (r["person_id"], r["topic_norm"], r["polarity"])


def _body(conn, table, r) -> str:
    if table == "fact":
        subject = _person(conn, r["subject_id"]) if r["subject_kind"] == "person" else r["subject_kind"]
        return " ".join(x for x in (subject, predicate_words(r["predicate_id"]), _s(r["object_text"])) if x)
    if table == "edge":
        return "%s person relation to %s as %s" % (_person(conn, r["from_person"]),
                                                  _person(conn, r["to_person"]), r["relation_id"])
    return " ".join(x for x in (_person(conn, r["person_id"]), "prefers", _s(r["polarity"]),
                                _s(r["topic_norm"])) if x)


def _support(conn, table, row_id) -> list:
    link, col = _SPAN_LINK[table]
    return [x[0] for x in conn.execute(
        "select s.said_at from %s l join span s on s.id = l.span_id "
        "where l.%s = ? and l.role = 'support'" % (link, col), (row_id,))]


def _header(n) -> bytes:
    words = [MAGIC, VERSION, n % MAX_FACTS, n, 0] + [0] * 10
    cs = 0
    for w in words:
        cs ^= w
    return struct.pack("<16I", *(words + [cs]))


def build(db_path):
    """(image bytes, manifest) for the store at `db_path`, opened read-only."""
    # A network share is refused BEFORE the path is resolved or opened. On Windows `resolve()` keeps
    # the authority, so the read-only URI becomes file://server/... and SQLite rejects it ("invalid
    # uri authority"); on Linux the same backslash string is no share at all, its drive is empty,
    # and it would resolve under the current directory. The raw-string clause is what refuses it
    # on both, and the drive clause catches a share spelled any other way on Windows.
    raw = str(db_path)
    if raw.startswith("\\\\") or raw.startswith("//") or Path(raw).drive.startswith("\\\\"):
        raise ProjectionRefused("%s: %s" % (RULE_UNC_PATH, raw))
    conn = _connect_ro(db_path)
    try:
        rows = _select(conn)
        if len(rows) > MAX_FACTS:
            raise ProjectionRefused("%s: %d selected, the region holds %d" % (RULE_TOO_MANY, len(rows), MAX_FACTS))
        records, packed, seen = [], [], set()
        for slot, (table, r) in enumerate(rows):
            said = _support(conn, table, r["id"])
            if not said:
                raise ProjectionRefused("%s: %s %d" % (RULE_NO_SPAN, table, r["id"]))
            support = distinct_days(said)
            for s in said:              # EVERY supporting span, not only the newest: each is a support day
                try:
                    p = datetime.fromisoformat(s)
                except (ValueError, TypeError):
                    raise ProjectionRefused("%s: %s %d, %r" % (RULE_BAD_TIME, table, r["id"], s)) from None
                if calendar.timegm(p.utctimetuple()) < 0:
                    raise ProjectionRefused("%s: %s %d, %r" % (RULE_PRE_EPOCH, table, r["id"], s))
            newest = max(said)          # the string maximum; parsed once, after
            try:
                parsed = datetime.fromisoformat(newest)
            except (ValueError, TypeError):
                raise ProjectionRefused("%s: %s %d, %r" % (RULE_BAD_TIME, table, r["id"], newest)) from None
            t_ms = calendar.timegm(parsed.utctimetuple()) * 1000
            if t_ms < 0:
                raise ProjectionRefused("%s: %s %d, %r" % (RULE_PRE_EPOCH, table, r["id"], newest))
            conf_x100 = math.floor(r["confidence"] * 100 + 0.5)
            key_string = _key_string(table, r)
            text = "%s (%s, %s)" % (_body(conn, table, r), r["source_kind"],
                                    "1 day" if support == 1 else "%d days" % support)
            if not (_printable(key_string) and _printable(text)):
                raise ProjectionRefused("%s: %s %d" % (RULE_NOT_PRINTABLE, table, r["id"]))
            tb = text.encode("ascii")
            if len(tb) > TEXT_MAX:
                raise ProjectionRefused("%s: %s %d is %d bytes" % (RULE_TOO_LONG, table, r["id"], len(tb)))
            key = fnv1a64(key_string.encode("ascii"))
            if key in seen:
                raise ProjectionRefused("%s: %s %d, %r" % (RULE_DUPLICATE_KEY, table, r["id"], key_string))
            seen.add(key)
            rec = (struct.pack(REC_FMT, 0, slot + 1, t_ms, key, FACT_PROFILE, support, conf_x100, len(tb))
                   + tb.ljust(TEXT_MAX, b"\0") + bytes(40))
            assert len(rec) == SECTOR
            packed.append(rec)
            records.append({"slot": slot, "seq": slot + 1, "table": table, "row_id": r["id"],
                            "key_string": key_string, "key": "%016x" % key, "fact_type": FACT_PROFILE,
                            "support_count": support, "confidence_x100": conf_x100, "t_ms": t_ms,
                            "text": text})
    finally:
        conn.close()
    n = len(records)
    img = bytearray(REGION_BYTES)
    img[0:64] = _header(n)
    for slot, rec in enumerate(packed):
        img[(slot + 1) * SECTOR:(slot + 2) * SECTOR] = rec
    data = bytes(img)
    manifest = {
        "n": n,
        "bytes": len(data),
        "md5_header": hashlib.md5(data[:SECTOR]).hexdigest(),
        "md5_records": hashlib.md5(data[SECTOR:]).hexdigest(),
        "md5_image": hashlib.md5(data).hexdigest(),
        "records": records,
    }
    return data, manifest


def _inside_repo(path) -> bool:
    p = os.path.normcase(str(Path(path).resolve()))
    root = os.path.normcase(str(REPO_ROOT))
    return p == root or p.startswith(root.rstrip(os.sep) + os.sep)


def write_image(path, data):
    """Write the image to a file OUTSIDE the repository, binary, every byte, size checked."""
    if _inside_repo(path):
        raise ProjectionRefused("%s: %s" % (RULE_INSIDE_REPO, Path(path).resolve()))
    if len(data) != REGION_BYTES:
        raise ValueError("an image is exactly %d bytes, got %d" % (REGION_BYTES, len(data)))
    fd = os.open(str(path), os.O_WRONLY | os.O_CREAT | os.O_TRUNC | getattr(os, "O_BINARY", 0), 0o644)
    try:
        view, off = memoryview(data), 0
        while off < len(data):
            n = os.write(fd, view[off:])
            if n <= 0:
                raise OSError("write returned %d at offset %d" % (n, off))
            off += n
        size = os.fstat(fd).st_size
        if size != REGION_BYTES:
            raise OSError("the image file is %d bytes, expected %d" % (size, REGION_BYTES))
    finally:
        os.close(fd)
