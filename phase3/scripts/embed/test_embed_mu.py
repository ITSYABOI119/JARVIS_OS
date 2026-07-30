#!/usr/bin/env python3
"""
THE ANCHOR: prove phase3/src/ai/embed_mu.h holds golden_vectors.npz's `mu` bit-for-bit.

Run:  python3 phase3/scripts/embed/test_embed_mu.py

WHAT THIS IS ANCHORED TO, stated plainly because it is the whole question
-------------------------------------------------------------------------
The comparison is against **golden_vectors.npz itself** -- the committed 66 KB
artifact -- read as RAW BYTES with the stdlib (zipfile + struct, no numpy). There
is no intermediate blob, and nothing here is derived from the header it checks.

That matters because the weak version of this task is a test that compares the
header against something the generator also produced from the header's own values:
it would pass whatever the generator did. Here the two sides are (a) the npz's raw
little-endian float32 bytes and (b) the hex float literals parsed out of the
header's text, and they are compared as BIT PATTERNS. A float tolerance would pass
the exact bug this exists to prevent, so there is no tolerance anywhere in this
file.

The C side (test_embed_mu.c) checks a different thing -- that the COMPILER parses
those literals back to the intended bits -- and it anchors to EMBED_MU_XOR, which
T4 below verifies against the npz. So the C check chains back to the npz through a
CI-verified constant rather than through anything self-referential.

Stdlib only: CI installs no numpy, and a check that only runs on a developer
machine is a check nobody re-runs.
"""

import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
HEADER = os.path.join(REPO, "phase3", "src", "ai", "embed_mu.h")
GEN = os.path.join(HERE, "gen_embed_mu.py")

sys.path.insert(0, HERE)
from gen_embed_mu import read_npy_member, bits_of, NPZ, MU_MEMBER, EXPECT_DIM  # noqa: E402

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
    produced 'FAIL: header bit patterns == npz' immediately followed by
    'PASS: T2 BIT-EXACT: all 1024 ... match' -- the exit code was still 1, but a
    line saying PASS on a failing check is the kind of output that gets quoted.
    """
    if failed == _mark[0]:
        print(f"  PASS: {msg}")
    else:
        print(f"  ---- {msg.split(':')[0]}: NOT PASSED (see FAIL above)")


def parse_header_floats(path):
    """Extract the EMBED_MU initialiser's literals and parse them INDEPENDENTLY.

    Deliberately re-parses the header's TEXT rather than importing anything from
    the generator's formatting path -- the point is to check what a compiler would
    actually see. float.fromhex is used on the literal with the 'f' suffix
    stripped; a decimal literal would be caught by T6.
    """
    with open(path, "r") as f:
        text = f.read()
    m = re.search(r"static const float EMBED_MU\[EMBED_MU_DIM\] = \{(.*?)\n\};",
                  text, re.S)
    if not m:
        raise SystemExit("FAIL: could not find the EMBED_MU initialiser")
    body = m.group(1)
    lits = [t.strip() for t in body.replace("\n", " ").split(",") if t.strip()]
    vals = []
    for lit in lits:
        if not lit.endswith("f"):
            raise SystemExit(f"FAIL: literal {lit!r} lacks the float suffix")
        core = lit[:-1]
        if not core.lower().startswith(("0x", "-0x", "+0x")):
            raise SystemExit(f"FAIL: literal {lit!r} is not a hex float literal")
        # Round to binary32, which is what the 'f' suffix makes the compiler do.
        vals.append(struct.unpack("<f", struct.pack("<f", float.fromhex(core)))[0])
    return vals, lits, text


def main():
    print("=== JARVIS AI-OS: embed_mu.h <-> golden_vectors.npz bit-exactness ===\n")

    mu_npz, hdr, data = read_npy_member(NPZ, MU_MEMBER)
    mu_hdr, lits, text = parse_header_floats(HEADER)

    # ---- T1: shape ----
    begin()
    check(len(mu_npz) == EXPECT_DIM, f"npz has {len(mu_npz)} values, expected {EXPECT_DIM}")
    check(len(mu_hdr) == EXPECT_DIM, f"header has {len(mu_hdr)} literals, expected {EXPECT_DIM}")
    check(len(data) == EXPECT_DIM * 4, "npz mu is exactly 4096 raw bytes")
    done(f"T1 shape ({EXPECT_DIM} values both sides, {len(data)} raw npz bytes)")

    # ---- T2: THE BIT-EXACT COMPARISON. memcmp on the packed bytes, no tolerance ----
    begin()
    npz_bytes = struct.pack("<%df" % EXPECT_DIM, *mu_npz)
    hdr_bytes = struct.pack("<%df" % EXPECT_DIM, *mu_hdr)
    check(npz_bytes == data, "repacking the npz values reproduces the raw npz bytes")
    check(hdr_bytes == npz_bytes, "header bit patterns == npz bit patterns")
    if hdr_bytes != npz_bytes:
        for i, (a, b) in enumerate(zip(mu_npz, mu_hdr)):
            if bits_of(a) != bits_of(b):
                print(f"    first diff [{i}]: npz 0x{bits_of(a):08X} vs hdr 0x{bits_of(b):08X}")
                break
    done("T2 BIT-EXACT: all 1024 header literals match the npz bit-for-bit")

    # ---- T3: the ULP control. A comparison that always passes is indistinguishable
    #          from one that works, so perturb by ONE ULP and require rejection. ----
    begin()
    idx = 500
    orig_bits = bits_of(mu_npz[idx])
    pert = list(mu_npz)
    pert[idx] = struct.unpack("<f", struct.pack("<I", orig_bits + 1))[0]
    pert_bytes = struct.pack("<%df" % EXPECT_DIM, *pert)
    check(pert_bytes != npz_bytes, "a 1-ULP perturbation changes the packed bytes")
    check(pert_bytes != hdr_bytes,
          "TEETH: the comparison REJECTS a 1-ULP perturbation")
    # And it is genuinely one ULP, not a larger accidental edit.
    check(bits_of(pert[idx]) - orig_bits == 1, "the perturbation is exactly 1 ULP")
    check(abs(pert[idx] - mu_npz[idx]) > 0.0, "1 ULP is a real, non-zero change")
    delta = abs(pert[idx] - mu_npz[idx])
    done(f"T3 ULP control (idx {idx}: 0x{orig_bits:08X} -> "
          f"0x{orig_bits + 1:08X}, delta {delta:.3e}, comparison REJECTS)")

    # ---- T4: EMBED_MU_XOR is correct w.r.t. the NPZ. This is what lets the C test
    #          chain back here instead of to something derived from the header. ----
    begin()
    m = re.search(r"#define\s+EMBED_MU_XOR\s+0x([0-9A-Fa-f]{8})u", text)
    check(m is not None, "EMBED_MU_XOR is present and well-formed")
    xor_npz = 0
    for v in mu_npz:
        xor_npz ^= bits_of(v)
    if m:
        check(int(m.group(1), 16) == xor_npz,
              f"EMBED_MU_XOR 0x{m.group(1)} == npz XOR 0x{xor_npz:08X}")
    # ALSO check it against the HEADER's own literals. Without this, a mutated
    # header leaves T4 green (the constant still matches the npz, which is
    # unchanged) and the C side's anchor loses a detection it should have.
    xor_hdr = 0
    for v in mu_hdr:
        xor_hdr ^= bits_of(v)
    check(xor_hdr == xor_npz, "XOR over the header's literals == npz XOR")
    # Teeth for the XOR too: a 1-ULP change must move it.
    xor_pert = 0
    for v in pert:
        xor_pert ^= bits_of(v)
    check(xor_pert != xor_npz, "TEETH: a 1-ULP perturbation changes the XOR")
    done(f"T4 EMBED_MU_XOR == 0x{xor_npz:08X}, verified against the npz "
          f"(and moves under 1 ULP)")

    # ---- T5: no decimal literals anywhere in the array ----
    begin()
    bad = [l for l in lits if not l[:-1].lower().lstrip("+-").startswith("0x")]
    check(not bad, f"{len(bad)} non-hex literals found")
    check(all(l.endswith("f") for l in lits), "every literal carries the float suffix")
    done("T5 every literal is a hex float with an 'f' suffix (no decimal)")

    # ---- T6: the header is not stale w.r.t. the npz (the drift gate) ----
    begin()
    r = subprocess.run([sys.executable, GEN, "--check"],
                       capture_output=True, text=True)
    check(r.returncode == 0, f"gen_embed_mu.py --check failed: {r.stdout}{r.stderr}")
    done("T6 generator --check agrees the header is current")

    # ---- T7: THE RENORMALISATION IS PRECISION-DEPENDENT, and this is the finding
    #          that most needs to survive.
    #
    #          cm2_floor.py:59 renormalises before projecting, so the 0.55 floor was
    #          measured with mu/||mu||, not with the stored bits. But whether that
    #          division changes anything depends on how the norm is accumulated, and
    #          both branches are re-derived here rather than asserted from a comment,
    #          so a regeneration cannot leave the header's numbers stale. ----
    begin()
    def f32(x):
        return struct.unpack("<f", struct.pack("<f", x))[0]

    def moved_under(norm):
        return sum(1 for v in mu_npz if bits_of(f32(v / norm)) != bits_of(v))

    n2 = sum(float(v) * float(v) for v in mu_npz)
    check(n2 != 1.0, "the stored mu is NOT exactly unit-norm in double")
    check(abs(n2 - 1.0) < 1e-6, "but it is unit to within 1e-6 (it IS a direction)")

    # Branch A: accumulate in double, then round -- the natural C implementation.
    norm_dbl = f32(n2 ** 0.5)
    moved_dbl = moved_under(norm_dbl)
    check(norm_dbl == 1.0, "a double-accumulated norm rounds to EXACTLY 1.0f")
    check(moved_dbl == 0, f"double-accumulated renorm is a no-op (moved {moved_dbl})")

    # Branch B: accumulate in float32 -- what numpy did, so what was measured.
    s = 0.0
    for v in mu_npz:
        s = f32(s + f32(v * v))
    norm_f32 = f32(s ** 0.5)
    moved_f32 = moved_under(norm_f32)
    check(norm_f32 != 1.0, "a float32-accumulated norm is NOT 1.0f")
    check(moved_f32 == EXPECT_DIM,
          f"float32-accumulated renorm moves every element (moved {moved_f32})")

    # THE POINT: the two branches disagree, so "renormalise" is not a precision-free
    # instruction. A consumer must know which one it implemented.
    check(moved_dbl != moved_f32,
          "TEETH: the two accumulation precisions give DIFFERENT results")
    done(f"T7 renorm is PRECISION-DEPENDENT: ||mu||^2 = {n2!r}; "
          f"double-norm {norm_dbl!r} moves {moved_dbl}/{EXPECT_DIM}, "
          f"f32-norm {norm_f32!r} moves {moved_f32}/{EXPECT_DIM}")

    print(f"\n== {passed} PASS, {failed} FAIL ==")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
