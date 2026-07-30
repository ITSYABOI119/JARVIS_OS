#!/usr/bin/env python3
"""
Generate phase3/src/ai/embed_mu.h from golden_vectors.npz's frozen `mu`.

WHY THIS EXISTS
---------------
`mu` -- the frozen mean-projection direction -- existed ONLY as a (1024,) float32
array inside phase3/scripts/embed/golden_vectors.npz. Nothing in phase3/src could
read it, so the deployed pipeline could not reproduce the numbers that justified
the 0.55 similarity floor. And mean-projection is not optional: cm2_floor_results
.json measures the RAW configs at 12.5-28.6% false recalls at every usable floor
against 0 observed for mean-projected. Without `mu` on the box path, semantic
recall is either inert or unsafe.

Mechanism follows the established precedent -- a committed generator plus a
generated header, exactly as gen_control_vectors.py produces sha256_vectors.h /
hmac_vectors.h. 4 KB of frozen constants belongs in version control, not in a
raw-LBA sector that would need provisioning, a read path, and a failure mode, for
a value that never changes.

DIMENSION: `mu` is applied at 1024, BEFORE Matryoshka truncation to 128
(cm2_floor.py's documented order: embed(1024) -> meanproj in 1024 -> truncate ->
L2). So this is a 1024-float artifact, matching ipc/embed_region.h's EMBED_DIM and
NOT g3_retrieval.h's EMBED_STORE_DIM/G3_SEM_DIM_DEFAULT of 128. Both constants are
correct and distinct; 4f3474f documents why.

STDLIB ONLY -- DELIBERATELY, AND IT IS LOAD-BEARING
---------------------------------------------------
No numpy. An .npz is a ZIP of .npy members, and a .npy is a short ASCII header
followed by raw little-endian data, so zipfile + struct reads it exactly. Two
reasons this matters rather than being a style choice:

  1. CI has no numpy (checked: ci.yml installs only pyserial, psutil, playwright).
     A numpy dependency would either add an install step or push the verification
     out of CI into a one-shot -- and a one-shot check is one nobody re-runs.
  2. Reading the RAW BYTES is a stronger anchor than trusting numpy's
     interpretation of them. The thing being verified is a bit pattern.

LITERAL FORM: C99 HEX FLOAT (`%a`), NOT DECIMAL
-----------------------------------------------
Hex float literals are exact by construction -- the mantissa is written in base 2,
so no rounding step exists anywhere in the chain.

A correction worth recording, because the usual justification for this choice is
slightly wrong: decimal is NOT actually unsafe at 9 significant digits.
FLT_DECIMAL_DIG is 9 precisely because 9 digits round-trip binary32, and measured
over this exact mu, "%.9g" produced 0/1024 round-trip failures. So decimal would
have worked here. Hex is still the right choice, on a better argument: it needs no
assumption that printf and the compiler's parser are both correctly rounding, and
it cannot silently degrade if a future regeneration runs on a different libc.

Emitted as `static const float` rather than a uint32_t bit-pattern array so the
box can take `const float *` for its dot product with ZERO runtime unpack -- a
bit-pattern array would need a 4 KB unpack buffer in PA or PB. The cost of that
choice is that the compiler must parse the literals back to the intended bits, so
that parse is VERIFIED rather than assumed: EMBED_MU_XOR carries the XOR of the
intended bit patterns and test_embed_mu.c recomputes it from the compiled array.

Usage:
    python3 phase3/scripts/embed/gen_embed_mu.py            # write the header
    python3 phase3/scripts/embed/gen_embed_mu.py --check     # drift gate (CI)
    python3 phase3/scripts/embed/gen_embed_mu.py --print-xor # the XOR constant
"""

import argparse
import ast
import hashlib
import os
import struct
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
NPZ = os.path.join(HERE, "golden_vectors.npz")
HEADER = os.path.join(REPO, "phase3", "src", "ai", "embed_mu.h")
META = os.path.join(HERE, "golden_meta.json")

MU_MEMBER = "mu.npy"
EXPECT_DIM = 1024
EXPECT_DTYPE = "<f4"


def read_npy_member(npz_path, member):
    """Parse one .npy member of an .npz with the stdlib only.

    Returns (list_of_float, header_dict, raw_data_bytes). Validates the magic,
    dtype and shape rather than assuming them -- a silently transposed or
    double-precision array would otherwise produce a plausible-looking header.
    """
    with zipfile.ZipFile(npz_path) as z:
        if member not in z.namelist():
            raise SystemExit(f"FAIL: {member} not in {npz_path} ({z.namelist()})")
        raw = z.read(member)

    if raw[:6] != b"\x93NUMPY":
        raise SystemExit(f"FAIL: {member} is not a .npy (magic {raw[:6]!r})")
    major = raw[6]
    if major == 1:
        hlen = int.from_bytes(raw[8:10], "little")
        off = 10 + hlen
    else:
        hlen = int.from_bytes(raw[8:12], "little")
        off = 12 + hlen
    hdr = ast.literal_eval(raw[off - hlen:off].decode("latin1").strip())

    if hdr["descr"] != EXPECT_DTYPE:
        raise SystemExit(f"FAIL: dtype is {hdr['descr']!r}, expected {EXPECT_DTYPE!r}")
    if hdr["fortran_order"]:
        raise SystemExit("FAIL: fortran_order is True")
    if tuple(hdr["shape"]) != (EXPECT_DIM,):
        raise SystemExit(f"FAIL: shape is {hdr['shape']}, expected ({EXPECT_DIM},)")

    data = raw[off:]
    if len(data) != EXPECT_DIM * 4:
        raise SystemExit(f"FAIL: {len(data)} data bytes, expected {EXPECT_DIM * 4}")
    return list(struct.unpack("<%df" % EXPECT_DIM, data)), hdr, data


def bits_of(f):
    """The IEEE-754 binary32 bit pattern of a Python float, as a uint32."""
    return struct.unpack("<I", struct.pack("<f", f))[0]


def hex_literal(f):
    """A C99 hex float literal that parses back to exactly this binary32 value.

    float.hex() gives the DOUBLE-precision hex form (13 mantissa hex digits). The
    value came from a binary32, so at most 6 of those carry information and the
    rest are exactly zero -- trimming them is lossless and keeps the header
    readable. Both the trim and the round-trip are VERIFIED per value below rather
    than argued: the whole point of this task is that a near-enough mu silently
    moves the operating point the 0.55 floor was measured at.
    """
    h = float.hex(f)                          # e.g. '-0x1.9af9840000000p-6'
    mant, _, exp = h.partition("p")
    intpart, _, frac = mant.partition(".")
    frac = frac.rstrip("0") or "0"            # keep one digit so '0x1.0p+0' stays valid C
    lit = f"{intpart}.{frac}p{exp}"

    # The trimmed literal must parse to the identical binary32. Checked, not assumed.
    if struct.pack("<f", float.fromhex(lit)) != struct.pack("<f", f):
        raise SystemExit(f"FAIL: trimmed hex literal {lit} does not round-trip {f!r}")
    return lit + "f"


def build_header(mu, npz_sha, data_bytes):
    xor = 0
    for v in mu:
        xor ^= bits_of(v)

    # Norm in double, reported as provenance. The stored mu is NOT exactly
    # unit-norm, which is why renormalisation-at-use is mandatory (see below).
    n2 = sum(float(v) * float(v) for v in mu)

    lines = []
    A = lines.append
    A("/*")
    A(" * JARVIS AI-OS - Phase C: the frozen mean-projection direction `mu`")
    A(" *")
    A(" * GENERATED FILE - DO NOT EDIT BY HAND.")
    A(" *   generator: phase3/scripts/embed/gen_embed_mu.py")
    A(" *   drift gate: `gen_embed_mu.py --check` (CI)")
    A(" *   verified against the npz bit-for-bit by phase3/scripts/embed/test_embed_mu.py (CI)")
    A(" *")
    A(" * PROVENANCE")
    A(" *   source      phase3/scripts/embed/golden_vectors.npz :: mu.npy")
    A(f" *   npz sha256  {npz_sha}")
    A(f" *   dtype/shape {EXPECT_DTYPE} ({EXPECT_DIM},) = {len(data_bytes)} raw bytes")
    A(" *   model       Qwen/Qwen3-Embedding-0.6B (see golden_meta.json for the")
    A(" *               full identity - never quote model identity from memory)")
    A(f" *   ||mu||^2    {n2!r} (double-precision sum of the stored values)")
    A(" *")
    A(" * FROZEN. Regenerating `mu` from a different embedder, a different corpus, or")
    A(" * a different pooling configuration INVALIDATES cm2_floor_results.json and")
    A(" * therefore invalidates the 0.55 similarity floor in g3_retrieval.h. The floor")
    A(" * is not a tuned constant; it is a measurement taken against this projection.")
    A(" * A new mu means a new measurement, not a new header.")
    A(" *")
    A(" * ==================== RENORMALISE AT USE. NOT OPTIONAL. ====================")
    A(" *")
    A(" * The transform is  e' = normalize(e - (e . mu_hat) * mu_hat)  where")
    A(" * mu_hat = mu / ||mu||  (cm2_floor.py:57-60, and golden_meta.json's formula).")
    A(" *")
    A(" * THE VALUES BELOW ARE THE npz BITS, WHICH ARE **NOT** mu_hat. The 0.55 floor")
    A(" * was measured with mu_hat.")
    A(" *")
    A(" * Storing the npz bits anyway is deliberate. They are the frozen, committed,")
    A(" * CI-verifiable artifact; mu_hat is a derived value whose exact bits depend on")
    A(" * the norm's accumulation precision (below), so storing it would bake an")
    A(" * implementation detail into the header and make it un-checkable against the")
    A(" * npz. Storing mu and renormalising at use reproduces the same TRANSFORM.")
    A(" *")
    A(" * ---- WHETHER THE RENORMALISATION DOES ANYTHING IS PRECISION-DEPENDENT ----")
    A(" *")
    A(" * MEASURED over this exact mu (test_embed_mu.py T7 re-derives all of it, so a")
    A(" * regeneration cannot leave these numbers stale):")
    A(" *")
    A(" *   norm accumulated in DOUBLE   -> 0.9999999873, which ROUNDS TO EXACTLY 1.0f")
    A(" *                                -> dividing by it moves 0 / 1024 elements.")
    A(" *                                   The renormalisation is a NO-OP and you get")
    A(" *                                   these stored bits straight back.")
    A(" *   norm accumulated in FLOAT32  -> 0.99999994 (numpy) / 0.99999952 (naive")
    A(" *                                   sequential) -- neither is 1.0f")
    A(" *                                -> dividing by it moves 1024 / 1024 elements,")
    A(" *                                   each by exactly 1 ULP.")
    A(" *")
    A(" * cm2_floor.py ran numpy on a float32 array, so the measurement used the")
    A(" * SECOND row. A C implementation that accumulates a 1024-term sum in double --")
    A(" * the natural choice -- lands on the FIRST row and gets the stored bits back.")
    A(" *")
    A(" * CONSEQUENCE, and it is small: 1 ULP is ~6e-8 relative, mu enters the")
    A(" * projection quadratically, so the effect on any cosine is ~1e-7 against a")
    A(" * floor whose measured margins are two orders of magnitude larger. THE CHOICE")
    A(" * IS IMMATERIAL TO THE OPERATING POINT. It is documented so that a later")
    A(" * session comparing box output against the Python pipeline does not mistake a")
    A(" * 1-ULP difference for a port bug -- the C/M1a GATE 2 lesson, where a tolerance")
    A(" * whose provenance was unknown made a correct port look broken.")
    A(" *")
    A(" * GUIDANCE FOR C/M2b: renormalise, because that is the documented transform,")
    A(" * and record which precision the norm was accumulated in. Do NOT skip it on the")
    A(" * grounds that ||mu|| is close to 1 -- that happens to be harmless here and it")
    A(" * is not the transform that was measured.")
    A(" *")
    A(" * HONEST LIMIT: bit-exact reproduction of the measurement's INPUTS is therefore")
    A(" * not available and is not claimed. What is claimed, and CI-verified against the")
    A(" * npz, is that these values are the npz's bits exactly and that the documented")
    A(" * transform is the same transform.")
    A(" *")
    A(" * DIMENSION: 1024, matching ipc/embed_region.h's EMBED_DIM. NOT the 128 of")
    A(" * g3_retrieval.h's G3_SEM_DIM_DEFAULT -- mu is applied BEFORE truncation.")
    A(" * Both constants are correct and distinct (4f3474f).")
    A(" */")
    A("")
    A("#ifndef EMBED_MU_H")
    A("#define EMBED_MU_H")
    A("")
    A("#include <stdint.h>")
    A('#include "../ipc/embed_region.h"   /* EMBED_DIM - mu lives in the transport\'s space */')
    A("")
    A(f"#define EMBED_MU_DIM  {EXPECT_DIM}")
    A("")
    A("/* XOR of the INTENDED binary32 bit patterns, computed by the generator from the")
    A(" * npz. test_embed_mu.c recomputes it from the compiled array, which is what")
    A(" * turns 'hex literals are exact' from an assumption into a verified property;")
    A(" * test_embed_mu.py checks this constant against the npz, so the C side's check")
    A(" * chains back to the npz rather than to anything derived from this header. */")
    A(f"#define EMBED_MU_XOR  0x{xor:08X}u")
    A("")
    A("static const float EMBED_MU[EMBED_MU_DIM] = {")
    for i in range(0, EXPECT_DIM, 4):
        chunk = ", ".join(hex_literal(mu[j]) for j in range(i, min(i + 4, EXPECT_DIM)))
        A(f"    {chunk},")
    A("};")
    A("")
    A('_Static_assert(sizeof(EMBED_MU) / sizeof(EMBED_MU[0]) == EMBED_MU_DIM,')
    A('               "mu array length must be EMBED_MU_DIM");')
    A('_Static_assert(EMBED_MU_DIM == 1024, "mu is a 1024-dimension artifact");')
    A("/* mu is applied in the TRANSPORT's space, before truncation - so it must track")
    A(" * EMBED_DIM, and a change to either must break this build rather than silently")
    A(" * project in the wrong space. */")
    A('_Static_assert(EMBED_MU_DIM == (int)EMBED_DIM,')
    A('               "mu dimension must equal the transport EMBED_DIM (pre-truncation)");')
    A("")
    A("#endif /* EMBED_MU_H */")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--check", action="store_true",
                    help="regenerate in memory and fail if the on-disk header differs")
    ap.add_argument("--print-xor", action="store_true",
                    help="print the XOR of the npz bit patterns and exit")
    args = ap.parse_args()

    mu, _hdr, data = read_npy_member(NPZ, MU_MEMBER)
    npz_sha = hashlib.sha256(open(NPZ, "rb").read()).hexdigest()

    if args.print_xor:
        xor = 0
        for v in mu:
            xor ^= bits_of(v)
        print("0x%08X" % xor)
        return 0

    text = build_header(mu, npz_sha, data)

    if args.check:
        if not os.path.exists(HEADER):
            print(f"FAIL: {HEADER} does not exist - run without --check")
            return 1
        with open(HEADER, "r", newline="") as f:
            on_disk = f.read()
        # Compare CONTENT, not line endings. This machine has core.autocrlf=false
        # and no .gitattributes, so the header is pure LF -- but another clone with
        # autocrlf=true would check out CRLF and fail this gate for a reason that
        # has nothing to do with mu being stale. The distinction matters because a
        # staleness failure is supposed to mean "regenerate from the npz".
        if on_disk.replace("\r\n", "\n") != text.replace("\r\n", "\n"):
            print("FAIL: embed_mu.h is STALE - regenerate with gen_embed_mu.py")
            print(f"  on disk {len(on_disk)} chars, regenerated {len(text)} chars")
            return 1
        print(f"OK: embed_mu.h matches the npz-generated content "
              f"({EXPECT_DIM} values, npz {npz_sha[:12]}...)")
        return 0

    with open(HEADER, "w", newline="\n") as f:
        f.write(text)
    print(f"wrote {HEADER}")
    print(f"  {EXPECT_DIM} hex float literals from {MU_MEMBER}")
    print(f"  npz sha256 {npz_sha}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
