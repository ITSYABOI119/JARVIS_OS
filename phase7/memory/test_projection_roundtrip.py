#!/usr/bin/env python3
"""Python -> C round trip for the JSEM projection (Phase 7 MS3a).

The synthetic household (bench seed 1, 14 days, contract 6's committed extraction, the people layer,
no embedder) is run through the benchmark with its store persisted, projected by
jarvis_memory.project, and the image read back by the REAL semantic_store.c (test_semantic_store
--parse) and by phase3/scripts/parse_semantic.py. The expected aggregate numbers (among them 27
records, the image's 2,097,664 bytes and the three md5s) are the design's §9 MS3 pre-registration;
the 27 rows are the MS3a prompt's §4.6 table. Both were measured on the strategist's reference
projector, and this implementation must reproduce them.

Run from the repo root, in WSL or CI, with the compiled C suite:

    python3 phase7/memory/test_projection_roundtrip.py /tmp/tsem_proj

Standard library only. Every store and image is a temporary file outside the repository.
"""
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "phase7" / "memory"))
sys.path.insert(0, str(REPO / "phase3" / "scripts"))

from jarvis_memory import project  # noqa: E402
from jarvis_memory.bench import harness  # noqa: E402
import parse_semantic  # noqa: E402

CANDS = "phase7/memory/bench/results/ms2a4_c6_gemma-e4b-q8-q4tpl.json"
MD5_IMAGE = "10e4e0fe6ff9be360b14bb75ce68d039"
MD5_HEADER = "614e67a66fe3c3ab4b4ad094b73ed893"
MD5_RECORDS = "8a57e095ba1526d36e70b6098a666294"

# The pre-registered 27 records (the MS3a prompt's §4.6 table), verbatim: seq, key hex,
# support_count, confidence_x100, t_ms and the key string, then ' :: ', then the text.
EXPECTED = """
 1  175d274c5764de04  1  100  1772352470000  prof|fact|household:|household.topic|cycling  ::  household household topic cycling (stated_owner, 1 day)
 2  ea6554535a418e2c  1  100  1772525275000  prof|fact|household:|household.topic|pottery  ::  household household topic pottery (stated_owner, 1 day)
 3  e831badf3b5503dd  1  100  1772611600000  prof|fact|person:2|person.lives_in|sydney  ::  tess person lives in sydney (stated_other, 1 day)
 4  1ea542e940d93031  1  100  1772611610000  prof|fact|person:1|person.works_as|plumber  ::  alex person works as a plumber (stated_owner, 1 day)
 5  bdcaa6067a9c2ab4  1  100  1772698035000  prof|fact|person:1|person.habit|bakes on fridays  ::  alex person habit bakes on fridays (stated_owner, 1 day)
 6  4e8090fe6c5f9f72  1  100  1772698080000  prof|fact|household:|household.topic|gardening  ::  household household topic gardening (stated_owner, 1 day)
 7  3d8cc577bf51576d  1  100  1772698142000  prof|fact|person:2|person.trait|patient  ::  tess person trait patient (stated_owner, 1 day)
 8  f7cb15862d784964  1  100  1772784440000  prof|fact|person:1|person.habit|swims on sundays  ::  alex person habit swims on sundays (stated_owner, 1 day)
 9  8c7beb173c0064d9  1  100  1772784450000  prof|fact|person:2|person.habit|runs at dawn  ::  tess person habit runs at dawn (stated_other, 1 day)
10  8d5d04ca0c0a3e83  1  100  1772784455000  prof|fact|person:2|person.habit|walks the dog at seven  ::  tess person habit walks the dog at seven (stated_other, 1 day)
11  d2926b3d365b8997  1  100  1772784543000  prof|fact|person:1|person.trait|forgetful  ::  alex person trait forgetful (stated_other, 1 day)
12  66238c2a94051878  1  100  1772870860000  prof|fact|household:|household.routine|bins go out on tuesday  ::  household household routine the bins go out on tuesday (stated_owner, 1 day)
13  8425871dac9280ba  1  100  1772870885000  prof|fact|household:|household.topic|camping  ::  household household topic camping (stated_owner, 1 day)
14  d5421972415d4176  1  100  1772957265000  prof|fact|household:|household.routine|groceries arrive thursday  ::  household household routine groceries arrive thursday (stated_owner, 1 day)
15  8aee10aa5e1ff86a  1  100  1773043600000  prof|fact|person:1|person.lives_in|bendigo  ::  alex person lives in bendigo (stated_owner, 1 day)
16  c7d0cc8a7d4ed09f  1  100  1773043690000  prof|fact|household:|household.topic|renovations  ::  household household topic renovations (stated_owner, 1 day)
17  1eaaa53f2809298c  1  100  1773043741000  prof|fact|person:2|person.name|tess  ::  tess person name tess (stated_owner, 1 day)
18  433652836d0a6a8f  1  100  1773216495000  prof|fact|household:|household.topic|astronomy  ::  household household topic astronomy (stated_owner, 1 day)
19  5fca93881c71e78f  1  100  1773302820000  prof|fact|person:2|person.works_as|pharmacist  ::  tess person works as a pharmacist (stated_other, 1 day)
20  ecf57c2a96d9a6c3  1  100  1773302905000  prof|fact|household:|household.routine|school run  ::  household household routine the school run (stated_owner, 1 day)
21  c1b78153a7e9c568  6   81  1773389306000  prof|edge|1|2|partner  ::  alex person relation to tess as partner (inferred, 6 days)
22  e50a6c4c6818d311  3  100  1773043712000  prof|edge|2|1|spouse  ::  tess person relation to alex as spouse (stated_other, 3 days)
23  d7fb0517b6ab21ce  1  100  1772352520000  prof|pref|1|spicy food|likes  ::  alex prefers likes spicy food (stated_owner, 1 day)
24  9e7f0bac359588cb  1  100  1772525321000  prof|pref|1|loud music|likes  ::  alex prefers likes loud music (stated_owner, 1 day)
25  218a8cb1aacd2934  1  100  1772698122000  prof|pref|1|early mornings|likes  ::  alex prefers likes early mornings (stated_owner, 1 day)
26  2222349f2ef1255f  1  100  1772784523000  prof|pref|1|long drives|likes  ::  alex prefers likes long drives (stated_owner, 1 day)
27  c1b5d7560684dc6a  1  100  1772870931000  prof|pref|1|spicy food|dislikes  ::  alex prefers dislikes spicy food (stated_owner, 1 day)
"""

_passed = 0
_failed = 0


def check(name, cond, detail=""):
    global _passed, _failed
    if cond:
        _passed += 1
        print("PASS %s" % name)
    else:
        _failed += 1
        print("FAIL %s %s" % (name, detail))


def expected_rows():
    rows = []
    for line in EXPECTED.strip("\n").split("\n"):
        head, text = line.split("  ::  ", 1)
        seq, key, support, conf, t_ms, key_string = head.split(None, 5)
        rows.append({"seq": int(seq), "key": key, "support_count": int(support),
                     "confidence_x100": int(conf), "t_ms": int(t_ms), "key_string": key_string,
                     "text": text})
    return rows


def leaves(o, path=""):
    if isinstance(o, dict):
        out = {}
        for k in sorted(o):
            out.update(leaves(o[k], "%s/%s" % (path, k)))
        return out if o else {path: {}}
    if isinstance(o, list):
        out = {}
        for i, v in enumerate(o):
            out.update(leaves(v, "%s[%d]" % (path, i)))
        return out if o else {path: []}
    return {path: o}


def run_c(binary, img):
    p = subprocess.run([binary, "--parse", str(img)], capture_output=True, text=True)
    return p.returncode, p.stdout.split("\n")


def md5_file(path):
    return hashlib.md5(Path(path).read_bytes()).hexdigest()


def run_arm(out_db=None):
    return harness.run([1], 14, 0, None, True, None, "none", True, CANDS, "contract6", True,
                       out_db=out_db)


def main():
    if len(sys.argv) != 2:
        print("usage: test_projection_roundtrip.py <compiled test_semantic_store>")
        sys.exit(2)
    binary = sys.argv[1]
    with tempfile.TemporaryDirectory() as tdir:
        t = Path(tdir)

        print("== 1. neutrality")
        with_db = run_arm(out_db=str(t / "db1"))
        without = run_arm()
        la, lb = leaves(with_db), leaves(without)
        diff = sorted(k for k in set(la) | set(lb) if la.get(k, "<absent>") != lb.get(k, "<absent>"))
        print("   %d leaves with out_db, %d without; %d differ %s" % (len(la), len(lb), len(diff), diff[:10]))
        check("RT1 neutrality: run with out_db equals the same run without it, leaf for leaf",
              la == lb and len(la) > 0, repr(diff[:10]))
        store1 = t / "db1" / "household_seed1.sqlite"

        print("== 2. the build")
        img, man = project.build(store1)
        recs = man["records"]
        check("RT2a n is 27 and every key is distinct",
              man["n"] == 27 and len(recs) == 27 and len({r["key"] for r in recs}) == 27,
              repr((man["n"], len({r["key"] for r in recs}))))
        check("RT2b the image is 2,097,664 bytes", len(img) == 2097664 == man["bytes"], str(len(img)))
        check("RT2c md5 image %s, header %s, records %s" % (MD5_IMAGE, MD5_HEADER, MD5_RECORDS),
              (man["md5_image"], man["md5_header"], man["md5_records"]) == (MD5_IMAGE, MD5_HEADER, MD5_RECORDS)
              and hashlib.md5(img).hexdigest() == MD5_IMAGE,
              repr((man["md5_image"], man["md5_header"], man["md5_records"])))
        exp = expected_rows()
        fields = ("seq", "key", "support_count", "confidence_x100", "t_ms", "key_string", "text")
        bad = [(e["seq"], f, (r.get(f) if r else None), e[f]) for e, r in zip(exp, recs + [None] * 27)
               for f in fields if not r or r.get(f) != e[f]]
        check("RT2d the 27 records equal the pre-registered table on seq, key, support, "
              "confidence, t_ms, key string and text", len(exp) == 27 and not bad, repr(bad[:5]))
        check("RT2e slot is seq - 1, fact_type is 2 on every record, and the tables read fact x20, "
              "edge x2, preference x5",
              all(r["slot"] == r["seq"] - 1 and r["fact_type"] == 2 for r in recs)
              and [r["table"] for r in recs] == ["fact"] * 20 + ["edge"] * 2 + ["preference"] * 5,
              repr([r["table"] for r in recs]))

        print("== 3. a second build")
        run_arm(out_db=str(t / "db2"))
        img2, man2 = project.build(t / "db2" / "household_seed1.sqlite")
        check("RT3 a second build from a second out_db gives identical bytes",
              img2 == img and man2["md5_image"] == MD5_IMAGE, man2["md5_image"])

        print("== 4. the C round trip")
        path = t / "jsem_projection.img"
        project.write_image(path, img)
        before = md5_file(path)
        rc, lines = run_c(binary, path)
        check("RT4a test_semantic_store --parse exits 0", rc == 0, str(rc))
        check("RT4b line 1 reads count 27 header_total 27 boot_id_after 1",
              lines[0] == "count 27 header_total 27 boot_id_after 1", repr(lines[0]))
        want = ["%d 0 %d %d %s 2 %d %d %d %s" % (r["slot"], r["seq"], r["t_ms"], r["key"], r["support_count"],
                                                 r["confidence_x100"], len(r["text"]), r["text"]) for r in recs]
        got = [ln for ln in lines[1:] if ln]
        cbad = [(i, g, w) for i, (g, w) in enumerate(zip(got, want)) if g != w]
        check("RT4c every record line equals the manifest, with boot_id 0 and seq = slot + 1",
              len(got) == 27 and not cbad, repr((len(got), cbad[:3])))
        check("RT4d the image file is unchanged by --parse", md5_file(path) == before == MD5_IMAGE,
              md5_file(path))

        print("== 5. negative controls")
        flip = bytearray(img)
        flip[60] ^= 0x01
        p_flip = t / "flip.img"
        p_flip.write_bytes(bytes(flip))
        rc, lines = run_c(binary, p_flip)
        check("RT5a one bit flipped in byte 60: exit 5, count 0",
              rc == 5 and lines[0].startswith("count 0 "), repr((rc, lines[0])))
        ver = bytearray(img)
        ver[4] = 2
        words = [int.from_bytes(ver[i:i + 4], "little") for i in range(0, 60, 4)]
        cs = 0
        for w in words:
            cs ^= w
        ver[60:64] = cs.to_bytes(4, "little")
        p_ver = t / "version.img"
        p_ver.write_bytes(bytes(ver))
        hv = parse_semantic.parse_header(bytes(ver))
        rc, lines = run_c(binary, p_ver)
        check("RT5b version 2 with a recomputed checksum (only the version wrong): exit 5, count 0",
              hv["checksum_ok"] and hv["version"] == 2 and rc == 5 and lines[0].startswith("count 0 "),
              repr((hv["checksum_ok"], rc, lines[0])))
        p_short = t / "short.img"
        p_short.write_bytes(img[:-512])
        rc, lines = run_c(binary, p_short)
        check("RT5c a copy one sector short: exit 2 and no count line",
              rc == 2 and not any(ln.startswith("count ") for ln in lines), repr((rc, lines[:2])))

        print("== 6. the Python reader")
        pr = parse_semantic.iter_records(img)
        pw = [{"slot": r["slot"], "boot_id": 0, "seq": r["seq"], "t_ms": r["t_ms"], "key": int(r["key"], 16),
               "fact_type": 2, "support_count": r["support_count"], "confidence_x100": r["confidence_x100"],
               "text_len": len(r["text"]), "text": r["text"]} for r in recs]
        check("RT6 parse_semantic.iter_records over the image equals the manifest", pr == pw,
              repr([(a, b) for a, b in zip(pr, pw) if a != b][:2]))

    print()
    print("%d/%d checks passed" % (_passed, _passed + _failed))
    sys.exit(1 if _failed else 0)


if __name__ == "__main__":
    main()
