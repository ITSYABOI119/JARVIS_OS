#!/usr/bin/env python3
"""
THE ANCHOR: prove phase3/src/ai/route_centroids.h holds route_centroids.npz bit-for-bit.

Run:  python3 phase3/scripts/embed/test_route_centroids.py

WHAT THIS IS ANCHORED TO, stated plainly because it is the whole question
-------------------------------------------------------------------------
The comparison is against **route_centroids.npz itself** -- the committed anchor --
read as RAW BYTES with the stdlib (zipfile + struct, no numpy). There is no
intermediate blob, and nothing here is derived from the header it checks.

That matters because the weak version of this task is a test that compares the
header against something the generator also produced from the header's own values:
it would pass whatever the generator did. Here the two sides are (a) the npz's raw
little-endian float32 bytes and (b) the hex float literals parsed out of the
header's text, compared as BIT PATTERNS. A float tolerance would pass the exact bug
this exists to prevent, so there is no tolerance anywhere in this file.

WHAT IT DOES NOT PROVE, and no gate here claims otherwise: that the npz matches a
fresh embedding run. Re-embedding needs a 600 MB model and is not bit-reproducible
across torch/CUDA builds, so the npz IS the frozen anchor -- exactly as
golden_vectors.npz is for `mu`. This gate covers npz -> header. Corpus -> npz is
covered by the generator refusing to overwrite the npz without --force.

Stdlib only: CI installs no numpy (pyserial, psutil, playwright), and a check that
only runs on a developer machine is a check nobody re-runs. gen_route_centroids.py
keeps numpy and sentence_transformers inside its generate path for this reason, so
importing it here costs nothing.
"""

import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
HEADER = os.path.join(REPO, "phase3", "src", "ai", "route_centroids.h")
GEN = os.path.join(HERE, "gen_route_centroids.py")

sys.path.insert(0, HERE)
from gen_route_centroids import (  # noqa: E402
    read_npz, bits_of, NPZ, EXPECT_N, EXPECT_DIM, CLASS_ROWS, suite_sha256, SUITE_H)

passed = 0
failed = 0


def check(cond, msg):
    global passed, failed
    if cond:
        passed += 1
    else:
        failed += 1
        print(f"  FAIL: {msg}")


_mark = [0]


def begin():
    """Remember the failure count so a group's PASS line can be conditional."""
    _mark[0] = failed


def done(msg):
    """Print PASS only if nothing in this group failed.

    Without this the group summary printed unconditionally, so a mutated header
    produced a FAIL line immediately followed by a PASS line for the same check --
    the exit code was still 1, but a line saying PASS on a failing check is the kind
    of output that gets quoted. (test_embed_mu.py learned this the same way.)
    """
    if failed == _mark[0]:
        print(f"  PASS: {msg}")
    else:
        print(f"  ---- {msg.split(':')[0]}: NOT PASSED (see FAIL above)")


def parse_header_rows(path):
    """Extract ROUTE_CENTROIDS' literals and parse them INDEPENDENTLY.

    Re-parses the header's TEXT rather than importing anything from the generator's
    formatting path -- the point is to check what a compiler would actually see.

    The initialiser is NESTED (an array of arrays), so the rows are recovered by
    matching innermost {...} groups. That checks the STRUCTURE as well as the
    values: a header that flattened 384 literals into one brace group, or emitted
    two rows instead of three, fails here rather than sliding through on a total
    count that happens to match.
    """
    with open(path, "r") as f:
        text = f.read()
    m = re.search(
        r"static const float ROUTE_CENTROIDS\[ROUTE_CENTROIDS_N\]\[ROUTE_CENTROIDS_DIM\]"
        r"\s*=\s*\{(.*?)\n\};", text, re.S)
    if not m:
        raise SystemExit("FAIL: could not find the ROUTE_CENTROIDS initialiser")
    body = m.group(1)

    rows, all_lits = [], []
    for grp in re.findall(r"\{([^{}]*)\}", body):
        lits = [t.strip() for t in grp.replace("\n", " ").split(",") if t.strip()]
        vals = []
        for lit in lits:
            if not lit.endswith("f"):
                raise SystemExit(f"FAIL: literal {lit!r} lacks the float suffix")
            core = lit[:-1]
            if not core.lower().startswith(("0x", "-0x", "+0x")):
                raise SystemExit(f"FAIL: literal {lit!r} is not a hex float literal")
            # Round to binary32, which is what the 'f' suffix makes the compiler do.
            vals.append(struct.unpack("<f", struct.pack("<f", float.fromhex(core)))[0])
        rows.append(vals)
        all_lits.extend(lits)
    return rows, all_lits, text


def main():
    print("=== JARVIS AI-OS: route_centroids.h <-> route_centroids.npz bit-exactness ===\n")

    cents_npz, counts, suite_sha = read_npz(NPZ)
    rows_hdr, lits, text = parse_header_rows(HEADER)

    n_total = EXPECT_N * EXPECT_DIM
    flat_npz = [v for row in cents_npz for v in row]
    flat_hdr = [v for row in rows_hdr for v in row]

    # ---- T1: structure. 3 rows of 128 on BOTH sides, not 384 loose floats ----
    begin()
    check(len(cents_npz) == EXPECT_N, f"npz has {len(cents_npz)} rows, expected {EXPECT_N}")
    check(len(rows_hdr) == EXPECT_N, f"header has {len(rows_hdr)} rows, expected {EXPECT_N}")
    check(all(len(r) == EXPECT_DIM for r in cents_npz),
          "every npz row is EXPECT_DIM wide: " + str([len(r) for r in cents_npz]))
    check(all(len(r) == EXPECT_DIM for r in rows_hdr),
          "every header row is EXPECT_DIM wide: " + str([len(r) for r in rows_hdr]))
    check(len(lits) == n_total, f"header holds {len(lits)} literals, expected {n_total}")
    done(f"T1 structure ({EXPECT_N} rows x {EXPECT_DIM} both sides, {n_total} literals)")

    # ---- T2: THE BIT-EXACT COMPARISON. memcmp on the packed bytes, no tolerance ----
    begin()
    npz_bytes = struct.pack("<%df" % n_total, *flat_npz)
    hdr_bytes = struct.pack("<%df" % n_total, *flat_hdr)
    check(hdr_bytes == npz_bytes, "header bit patterns == npz bit patterns")
    if hdr_bytes != npz_bytes:
        for i, (a, b) in enumerate(zip(flat_npz, flat_hdr)):
            if bits_of(a) != bits_of(b):
                print(f"    first diff [{i // EXPECT_DIM}][{i % EXPECT_DIM}]: "
                      f"npz 0x{bits_of(a):08X} vs hdr 0x{bits_of(b):08X}")
                break
    # Per row too, so a diff report names the class rather than a flat index.
    for cid, name in CLASS_ROWS:
        check(struct.pack("<%df" % EXPECT_DIM, *rows_hdr[cid])
              == struct.pack("<%df" % EXPECT_DIM, *cents_npz[cid]),
              f"row {cid} ({name}) differs between header and npz")
    done(f"T2 BIT-EXACT: all {n_total} header literals match the npz bit-for-bit")

    # ---- T3: the ULP control. A comparison that always passes is indistinguishable
    #          from one that works, so perturb by ONE ULP and require rejection. ----
    begin()
    idx = 64            # inside row 0
    orig_bits = bits_of(flat_npz[idx])
    pert = list(flat_npz)
    pert[idx] = struct.unpack("<f", struct.pack("<I", orig_bits + 1))[0]
    pert_bytes = struct.pack("<%df" % n_total, *pert)
    check(pert_bytes != npz_bytes, "a 1-ULP perturbation changes the packed bytes")
    check(pert_bytes != hdr_bytes, "TEETH: the comparison REJECTS a 1-ULP perturbation")
    check(bits_of(pert[idx]) - orig_bits == 1, "the perturbation is exactly 1 ULP")
    check(abs(pert[idx] - flat_npz[idx]) > 0.0, "1 ULP is a real, non-zero change")
    delta = abs(pert[idx] - flat_npz[idx])
    done(f"T3 ULP control (flat idx {idx}: 0x{orig_bits:08X} -> 0x{orig_bits + 1:08X}, "
         f"delta {delta:.3e}, comparison REJECTS)")

    # ---- T4: ROUTE_CENTROIDS_XOR, checked BOTH ways ----
    #  against the npz  -> a C-side recomputation chains back to the anchor
    #  against the header's own literals -> a mutated header cannot leave this green
    begin()
    m = re.search(r"#define\s+ROUTE_CENTROIDS_XOR\s+0x([0-9A-Fa-f]{8})u", text)
    check(m is not None, "ROUTE_CENTROIDS_XOR is present and well-formed")
    xor_npz = 0
    for v in flat_npz:
        xor_npz ^= bits_of(v)
    if m:
        check(int(m.group(1), 16) == xor_npz,
              f"ROUTE_CENTROIDS_XOR 0x{m.group(1)} == npz XOR 0x{xor_npz:08X}")
    xor_hdr = 0
    for v in flat_hdr:
        xor_hdr ^= bits_of(v)
    check(xor_hdr == xor_npz, "XOR over the header's literals == npz XOR")
    xor_pert = 0
    for v in pert:
        xor_pert ^= bits_of(v)
    check(xor_pert != xor_npz, "TEETH: a 1-ULP perturbation changes the XOR")
    done(f"T4 ROUTE_CENTROIDS_XOR == 0x{xor_npz:08X}, verified against the npz AND the "
         f"header's literals (and moves under 1 ULP)")

    # ---- T5: no decimal literals anywhere in the array ----
    begin()
    bad = [x for x in lits if not x[:-1].lower().lstrip("+-").startswith("0x")]
    check(not bad, f"{len(bad)} non-hex literals found")
    check(all(x.endswith("f") for x in lits), "every literal carries the float suffix")
    done("T5 every literal is a hex float with an 'f' suffix (no decimal)")

    # ---- T6: the header's PROSE agrees with the npz.
    #          Per-row member counts and the DEV total are written into the banner, so
    #          they can go stale independently of the numbers. Check them. ----
    begin()
    check(len(counts) == EXPECT_N, f"npz counts has {len(counts)} entries")
    check(all(c > 0 for c in counts), f"every class has DEV members: {counts}")
    for cid, name in CLASS_ROWS:
        pat = r"/\* \[%d\] %s -- mean of (\d+) DEV cases \*/" % (cid, name)
        rm = re.search(pat, text)
        check(rm is not None, f"row {cid} ({name}) has its member-count comment")
        if rm:
            check(int(rm.group(1)) == counts[cid],
                  f"row {cid} comment says n={rm.group(1)}, npz says {counts[cid]}")
    check(re.search(r"\(%d DEV cases\)" % sum(counts), text) is not None,
          f"the banner records the DEV total {sum(counts)}")
    check(re.search(r"suite sha256 %s" % suite_sha, text) is not None,
          "the banner records the npz's suite sha256")
    done(f"T6 banner agrees with the npz (rows {counts}, total {sum(counts)})")

    # ---- T7: the rows are unit vectors.
    #          Not a bit-exactness check -- a property check. A regeneration that
    #          forgot the final L2 step, or averaged in the wrong space, produces a
    #          perfectly well-formed header whose every cosine is silently a magnitude
    #          ranking. That is the g3_vec_is_unit failure mode, one layer earlier. ----
    begin()
    worst = 0.0
    for cid, name in CLASS_ROWS:
        n2 = sum(float(v) * float(v) for v in cents_npz[cid])
        worst = max(worst, abs(n2 - 1.0))
        check(abs(n2 - 1.0) < 1e-6,
              f"row {cid} ({name}) ||c||^2 = {n2!r} is not unit to 1e-6")
        check(n2 != 1.0,
              f"row {cid} ||c||^2 is EXACTLY 1.0 -- suspicious for a float32 cast")
    done(f"T7 all {EXPECT_N} rows are unit vectors (worst |1-||c||^2| = {worst:.3e}; "
         f"not exactly 1.0, as the float32 cast implies)")

    # ---- T8: the drift gate ----
    begin()
    r = subprocess.run([sys.executable, GEN, "--check"], capture_output=True, text=True)
    check(r.returncode == 0, f"gen_route_centroids.py --check failed: {r.stdout}{r.stderr}")
    # The suite hash is ADVISORY in --check (a changed corpus does not make the header
    # stale w.r.t. the npz). Report it here so a divergence is visible rather than
    # buried in a subprocess's stdout.
    live = suite_sha256(SUITE_H)
    if live != suite_sha:
        print(f"  NOTE: routing_suite.h has changed since these centroids were fitted "
              f"({suite_sha[:12]}... -> {live[:12]}...). Not a failure: regenerating is a "
              f"re-MEASUREMENT, not a rebuild.")
    done("T8 generator --check agrees the header is current")

    print(f"\n== {passed} PASS, {failed} FAIL ==")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
