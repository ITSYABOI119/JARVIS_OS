#!/usr/bin/env python3
"""
Generate phase3/src/ai/route_centroids.h -- the FROZEN routing centroids (Phase C / C/M4).

WHY THIS EXISTS
---------------
cm4_routing_measure.py builds three class centroids in memory on every run, from
routing_suite.h's DEV split, and throws them away. That is fine for a measurement
and useless for anything downstream: nothing in phase3/src can read them, and a
re-run reproduces them only up to whatever the GPU, the torch build and the batch
order happen to do that day. A centroid that moves is a routing decision that
moves, silently.

So the three vectors are frozen here exactly as `mu` was: a committed generator, a
committed .npz anchor, a generated C header of hex-float literals, and a bit-exact
CI gate between them. This follows gen_embed_mu.py -> embed_mu.h step for step, and
that is deliberate -- the two artifacts are a matched pair (see mu-COUPLING below).

DEV SPLIT ONLY. NEVER HELDOUT.
------------------------------
The centroids are built from ROUTING_DEV and ONLY ROUTING_DEV. ROUTING_HELDOUT is
never read by this file -- not to build, not to tune, not to sanity-check. That is
routing_suite.h's own header rule ("NEVER edit HELDOUT to make route.c pass -- fix
route.c against the DEV split instead"), and it is what makes the held-out number
mean anything. A centroid fitted on HELDOUT would make the 95.89% self-referential.

THE STRING-SENTINEL TRAP -- READ BEFORE TOUCHING THE PIPELINE CALL
------------------------------------------------------------------
cm2_floor.prepare(emb, mu, dim, proj) tests `proj == "meanproj"`. It is a STRING
sentinel drawn from cm2_floor.PROJECTIONS = ["raw", "meanproj"]. Passing the bool
True is not an error, does not warn, and is byte-identical to passing "raw" --
mean-projection is SILENTLY SKIPPED. cm4_routing_measure.py:62 read `PROJ = True`
and therefore measured in raw space; it is corrected in the same commit as this
file. ALWAYS pass the string. Verified empirically before writing this line:
prepare(e, mu, 128, True) is np.array_equal to prepare(e, mu, 128, "raw"), and
differs from "meanproj" by up to 2.1e-2 per element.

That distinction is not cosmetic. cm2_floor_results.json measures the RAW configs
at 12.5-28.6% false recalls at every usable floor against 0 observed for
mean-projected, and the 0.55 floor in g3_retrieval.h was measured in the projected
space. A centroid set built in raw space would be a different artifact wearing the
same name.

STDLIB ONLY AT MODULE LEVEL -- LOAD-BEARING, NOT STYLE
-------------------------------------------------------
numpy, torch and sentence_transformers are imported INSIDE generate(), and
cm4_routing_measure.parse_suite is imported there too (it pulls numpy in
transitively). Nothing at module level needs more than the standard library.

The reason is the test: test_route_centroids.py must run in CI, CI installs no
numpy (ci.yml installs pyserial, psutil, playwright), and it imports this module
for the npz reader and the constants. One numpy import at module level here would
push the bit-exactness gate out of CI and into a one-shot that nobody re-runs. So
--check and --header-only work with zipfile + struct alone; only the generate path
needs the ML stack.

WHAT --check DOES AND DOES NOT PROVE
-------------------------------------
--check rebuilds the header text FROM route_centroids.npz and compares. It does
NOT re-embed. Re-embedding needs a 600 MB model and is not bit-reproducible across
torch/CUDA versions or batch orders, so a "regenerate and diff" gate would fail for
reasons that have nothing to do with the header being stale.

The honest statement, matching embed_mu.h's: the gate proves the HEADER matches the
NPZ. It does not prove the NPZ matches a fresh embedding run, and no gate here
claims to. The npz IS the frozen anchor, exactly as golden_vectors.npz is for mu.

Usage:
    python3 phase3/scripts/embed/gen_route_centroids.py            # embed + write both
    python3 phase3/scripts/embed/gen_route_centroids.py --force    # ... overwriting the npz
    python3 phase3/scripts/embed/gen_route_centroids.py --header-only  # npz -> header
    python3 phase3/scripts/embed/gen_route_centroids.py --check    # drift gate (CI)
    python3 phase3/scripts/embed/gen_route_centroids.py --print-xor
"""

import argparse
import ast
import hashlib
import os
import struct
import sys
import zipfile
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
NPZ = os.path.join(HERE, "route_centroids.npz")
HEADER = os.path.join(REPO, "phase3", "src", "ai", "route_centroids.h")
SUITE_H = os.path.join(REPO, "phase3", "src", "ai", "routing_suite.h")

sys.path.insert(0, HERE)
# bits_of / hex_literal are IMPORTED, not re-implemented. They are the
# exactness-critical half of this file, they are already CI-verified by
# test_embed_mu.py, and two copies of a float-formatting routine is two copies
# that can drift. gen_embed_mu.py is stdlib-only at module level, so this import
# does not break the no-numpy chain described above.
from gen_embed_mu import bits_of, hex_literal  # noqa: E402

EXPECT_N = 3            # one centroid per route_class_t
EXPECT_DIM = 128        # the C/M2-MEASURED space (g3_retrieval.h G3_SEM_DIM_DEFAULT)

# Row order IS route_class_t (route.h: ROUTE_INFER = 0, then SYSFACTS, DECLINE).
# The header pins this with a _Static_assert rather than a comment.
CLASS_ROWS = ((0, "ROUTE_INFER"), (1, "ROUTE_SYSFACTS"), (2, "ROUTE_DECLINE"))

_DTYPES = {"<f4": ("f", 4), "<i4": ("i", 4), "|u1": ("B", 1)}


def _parse_npy(raw, member):
    """Parse one .npy blob with the stdlib. Returns (values, shape, descr).

    Adapted from gen_embed_mu.read_npy_member, generalised to 1-D and 2-D and to
    the three dtypes this npz carries. Validates magic, dtype, order and shape
    rather than assuming them -- a transposed or float64 array would otherwise
    produce a plausible-looking header.
    """
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

    descr = hdr["descr"]
    if descr not in _DTYPES:
        raise SystemExit(f"FAIL: {member} dtype {descr!r} is not one of {sorted(_DTYPES)}")
    if hdr["fortran_order"]:
        raise SystemExit(f"FAIL: {member} fortran_order is True")

    code, width = _DTYPES[descr]
    shape = tuple(hdr["shape"])
    count = 1
    for d in shape:
        count *= d
    data = raw[off:]
    if len(data) != count * width:
        raise SystemExit(f"FAIL: {member} has {len(data)} data bytes, expected {count * width}")
    return list(struct.unpack("<%d%s" % (count, code), data)), shape, descr


def read_npz(path):
    """Read route_centroids.npz's three members. Stdlib only (zipfile + struct).

    Returns (centroids, counts, suite_sha_hex) where centroids is a list of
    EXPECT_N lists of EXPECT_DIM Python floats holding exact binary32 values.
    """
    want = ("centroids.npy", "counts.npy", "suite_sha256.npy")
    with zipfile.ZipFile(path) as z:
        names = z.namelist()
        for member in want:
            if member not in names:
                raise SystemExit(f"FAIL: {member} not in {path} ({names})")
        blobs = {m: z.read(m) for m in want}

    flat, shape, descr = _parse_npy(blobs["centroids.npy"], "centroids.npy")
    if descr != "<f4":
        raise SystemExit(f"FAIL: centroids dtype is {descr!r}, expected '<f4'")
    if shape != (EXPECT_N, EXPECT_DIM):
        raise SystemExit(f"FAIL: centroids shape is {shape}, expected {(EXPECT_N, EXPECT_DIM)}")
    cents = [flat[r * EXPECT_DIM:(r + 1) * EXPECT_DIM] for r in range(EXPECT_N)]

    counts, cshape, cdescr = _parse_npy(blobs["counts.npy"], "counts.npy")
    if cdescr != "<i4" or cshape != (EXPECT_N,):
        raise SystemExit(f"FAIL: counts is {cdescr} {cshape}, expected '<i4' ({EXPECT_N},)")

    sha, sshape, sdescr = _parse_npy(blobs["suite_sha256.npy"], "suite_sha256.npy")
    if sdescr != "|u1" or sshape != (32,):
        raise SystemExit(f"FAIL: suite_sha256 is {sdescr} {sshape}, expected '|u1' (32,)")

    return cents, [int(c) for c in counts], bytes(sha).hex()


def suite_sha256(path):
    """sha256 of routing_suite.h's LF-NORMALISED bytes.

    Normalised so a clone with core.autocrlf=true records the same provenance --
    the hash is meant to identify the CONTENT the centroids were fitted to, and a
    line-ending difference is not a different corpus.
    """
    with open(path, "rb") as f:
        return hashlib.sha256(f.read().replace(b"\r\n", b"\n")).hexdigest()


def build_header(cents, counts, suite_sha, npz_sha):
    """Render route_centroids.h. Pure function of the npz + the two hashes."""
    xor = 0
    for row in cents:
        for v in row:
            xor ^= bits_of(v)

    # ||c||^2 over the STORED float32 values, accumulated in double. Reported as
    # provenance: the rows are unit vectors before the float32 cast and very
    # slightly off unit after it, exactly as mu is.
    norms = [sum(float(v) * float(v) for v in row) for row in cents]

    members = ", ".join("%s n=%d" % (name, counts[cid]) for cid, name in CLASS_ROWS)

    lines = []
    A = lines.append
    A("/*")
    A(" * JARVIS AI-OS - Phase C: the FROZEN routing centroids (C/M4)")
    A(" *")
    A(" * GENERATED FILE - DO NOT EDIT BY HAND.")
    A(" *   generator:  phase3/scripts/embed/gen_route_centroids.py")
    A(" *   drift gate: `gen_route_centroids.py --check` (CI)")
    A(" *   verified against the npz bit-for-bit by")
    A(" *               phase3/scripts/embed/test_route_centroids.py (CI)")
    A(" *")
    A(" * WHAT THESE ARE")
    A(" *   Three unit vectors, one per route_class_t, living in the SAME 128-dimension")
    A(" *   mean-projected space g3_select_semantic compares in. A query embedded through")
    A(" *   that pipeline is classified by NEAREST CENTROID; cosine == dot because both")
    A(" *   sides are L2-normalised.")
    A(" *")
    A(" *   These are DATA, not a decision. Nothing here routes anything: whether a")
    A(" *   centroid may override, veto or merely inform route_classify is a separate")
    A(" *   question with its own measurement (cm4_routing_results.json).")
    A(" *")
    A(" * PROVENANCE")
    A(" *   source       phase3/src/ai/routing_suite.h :: ROUTING_DEV")
    A(f" *   suite sha256 {suite_sha}")
    A(" *                (LF-normalised bytes, so autocrlf does not change it)")
    A(" *   npz          phase3/scripts/embed/route_centroids.npz :: centroids.npy")
    A(f" *   npz sha256   {npz_sha}")
    A(" *   model        Qwen/Qwen3-Embedding-0.6B -- golden_meta.json carries the full")
    A(" *                identity (repo/file/quant/sha); never quote it from memory")
    A(" *   pipeline     embed(1024, sym:none, no prefix, normalize_embeddings=True,")
    A(" *                batch_size=16) -> mean-project with golden_vectors.npz's frozen")
    A(" *                mu -> truncate 128 -> L2 renormalise.  The generator IMPORTS")
    A(" *                cm2_floor.prepare rather than reimplementing it, so this cannot")
    A(" *                drift from the configuration the 0.55 floor was measured in.")
    A(" *                All of it in float64; the float32 cast happens once, at storage.")
    A(f" *   members      {members}  ({sum(counts)} DEV cases)")
    A(" *   ||c||^2      " + ", ".join("row%d %.12f" % (cid, norms[cid]) for cid, _ in CLASS_ROWS))
    A(" *                (double-precision sum over the STORED float32 values)")
    A(" *")
    A(" * ==================== DEV SPLIT ONLY. NEVER HELDOUT. ====================")
    A(" *")
    A(" * Built from ROUTING_DEV and only ROUTING_DEV. ROUTING_HELDOUT is not read by the")
    A(" * generator at all -- not to fit, not to tune, not to sanity-check. That is")
    A(" * routing_suite.h's own rule, and it is the entire reason the held-out number")
    A(" * means anything: a centroid fitted on HELDOUT would make it self-referential.")
    A(" *")
    A(" * ROW ORDER IS route_class_t")
    A(" *   [0] ROUTE_INFER   [1] ROUTE_SYSFACTS   [2] ROUTE_DECLINE")
    A(" * Compile-enforced by the _Static_asserts below, not merely asserted here -- a")
    A(" * silently permuted row set is a plausible-looking artifact that misroutes every")
    A(" * query, and no runtime check would catch it.")
    A(" *")
    A(" * ==================== FROZEN ====================")
    A(" *")
    A(" * Regenerating these vectors -- from a different embedder, a different DEV split,")
    A(" * a different pooling configuration, or the same one on a different GPU -- makes")
    A(" * every number measured against them stale, including the C/M4 arm results and")
    A(" * any probe-leg expectation derived from them. They are not tuned constants; they")
    A(" * are a measurement. A new corpus means a new measurement, not a new header.")
    A(" *")
    A(" * mu-COUPLING: these vectors live in the space `mu` defines. embed_mu.h and this")
    A(" * header are a MATCHED PAIR. A Matryoshka re-truncation, a new mu, or any change")
    A(" * to the projection must regenerate BOTH artifacts together -- regenerating one")
    A(" * alone leaves the box comparing vectors from two different spaces, which produces")
    A(" * confident, well-formed, wrong answers rather than an error.")
    A(" *")
    A(" * DIMENSION: 128, matching g3_retrieval.h's G3_SEM_DIM_DEFAULT -- the space AFTER")
    A(" * Matryoshka truncation. NOT embed_region.h's EMBED_DIM of 1024, which is where")
    A(" * mu is applied. Both constants are correct and distinct; embed_store.h:138")
    A(" * documents why they must not be reconciled to one value.")
    A(" *")
    A(" * RENORMALISE-AT-USE inherits embed_mu.h's guidance unchanged: the float32 cast")
    A(" * leaves ||c|| a hair off 1.0 (see the values above), so a consumer that needs a")
    A(" * true cosine should normalise, and should record which precision it accumulated")
    A(" * the norm in. The effect is ~1e-7 on any dot product -- immaterial to the")
    A(" * operating point, documented so a later session does not read it as a port bug.")
    A(" */")
    A("")
    A("#ifndef ROUTE_CENTROIDS_H")
    A("#define ROUTE_CENTROIDS_H")
    A("")
    A("#include <stdint.h>")
    A('#include "route.h"          /* route_class_t - row order is compile-enforced */')
    A('#include "g3_retrieval.h"   /* G3_SEM_DIM_DEFAULT - the measured 128-d space */')
    A("")
    A(f"#define ROUTE_CENTROIDS_N    {EXPECT_N}")
    A(f"#define ROUTE_CENTROIDS_DIM  {EXPECT_DIM}")
    A("")
    A("/* XOR of the INTENDED binary32 bit patterns, row-major, computed by the generator")
    A(" * from the npz. A C consumer can recompute it from the compiled array, which is")
    A(" * what turns 'hex literals are exact' from an assumption into a verified property;")
    A(" * test_route_centroids.py checks this constant against the npz AND against the")
    A(" * header's own literals, so any such C check chains back to the npz rather than to")
    A(" * something derived from this file. */")
    A(f"#define ROUTE_CENTROIDS_XOR  0x{xor:08X}u")
    A("")
    A("static const float ROUTE_CENTROIDS[ROUTE_CENTROIDS_N][ROUTE_CENTROIDS_DIM] = {")
    for cid, name in CLASS_ROWS:
        row = cents[cid]
        A(f"    /* [{cid}] {name} -- mean of {counts[cid]} DEV cases */")
        A("    {")
        for i in range(0, EXPECT_DIM, 4):
            chunk = ", ".join(hex_literal(row[j]) for j in range(i, min(i + 4, EXPECT_DIM)))
            A(f"        {chunk},")
        A("    },")
    A("};")
    A("")
    A("_Static_assert(sizeof(ROUTE_CENTROIDS) / sizeof(ROUTE_CENTROIDS[0]) == ROUTE_CENTROIDS_N,")
    A('               "centroid row count must be ROUTE_CENTROIDS_N");')
    A("_Static_assert(sizeof(ROUTE_CENTROIDS[0]) / sizeof(float) == ROUTE_CENTROIDS_DIM,")
    A('               "each centroid must be ROUTE_CENTROIDS_DIM floats");')
    A("/* The rows ARE route_class_t values. If a fourth class is ever appended, this")
    A(" * breaks the build instead of silently leaving the new class with no centroid")
    A(" * and every query for it falling to whichever row happens to score highest. */")
    A("_Static_assert(ROUTE_CENTROIDS_N == (int)ROUTE_DECLINE + 1,")
    A('               "a new route_class_t needs a new centroid row");')
    A("_Static_assert((int)ROUTE_INFER == 0 && (int)ROUTE_SYSFACTS == 1 && (int)ROUTE_DECLINE == 2,")
    A('               "centroid row order is route_class_t order");')
    A("/* The centroids are compared against vectors produced by the C/M2-measured")
    A(" * pipeline, so a change to either dimension must break this build rather than")
    A(" * silently compare across two different spaces. */")
    A("_Static_assert(ROUTE_CENTROIDS_DIM == G3_SEM_DIM_DEFAULT,")
    A('               "centroid dimension must equal the measured semantic space");')
    A("")
    A("#endif /* ROUTE_CENTROIDS_H */")
    return "\n".join(lines) + "\n"


def generate(force, cpu):
    """Embed ROUTING_DEV and write route_centroids.npz. Needs numpy + torch + ST.

    Imports live here, not at module level: test_route_centroids.py imports this
    module and must run in a CI that has no numpy (see the docstring).
    """
    if os.path.exists(NPZ) and not force:
        raise SystemExit(
            f"REFUSING to overwrite {NPZ} without --force.\n"
            "  These vectors are FROZEN. Unlike gen_embed_mu.py -- whose write is a\n"
            "  deterministic npz->header render -- this path runs model inference, so a\n"
            "  bare re-run on a different torch/CUDA build silently replaces the anchor\n"
            "  every downstream number was measured against.\n"
            "  To rebuild only the header text from the existing npz: --header-only")

    import numpy as np
    from sentence_transformers import SentenceTransformer
    # parse_suite pulls numpy in transitively via cm2_floor; imported here so the
    # module level stays stdlib-only.
    from cm4_routing_measure import parse_suite
    from cm2_floor import GOLDEN_NPZ, MODEL, prepare

    # DEV ONLY. parse_suite returns (dev, heldout); heldout is discarded ON THE SPOT
    # and is never bound to a name in this function.
    dev = parse_suite(Path(SUITE_H))[0]
    texts = [c["q"] for c in dev]
    counts = [sum(1 for c in dev if c["cls"] == cid) for cid, _ in CLASS_ROWS]
    print("ROUTING_DEV: %d cases -- %s"
          % (len(dev), ", ".join("%s n=%d" % (n, counts[c]) for c, n in CLASS_ROWS)))
    if sum(counts) != len(dev):
        raise SystemExit("FAIL: DEV holds a class outside route_class_t")
    if min(counts) == 0:
        raise SystemExit("FAIL: a route class has no DEV members -- cannot build its centroid")

    mu = np.load(GOLDEN_NPZ)["mu"].astype(np.float64)
    print("loading %s ..." % MODEL)
    m = SentenceTransformer(MODEL, device="cpu" if cpu else None)
    emb = np.asarray(m.encode(texts, normalize_embeddings=True, batch_size=16,
                              show_progress_bar=False), dtype=np.float64)
    if emb.shape[1] != 1024:
        raise SystemExit("FAIL: expected 1024-dim embeddings, got %d" % emb.shape[1])

    # THE STRING SENTINEL. prepare() tests `proj == "meanproj"`; passing True (or any
    # bool) silently skips mean-projection and is byte-identical to "raw". See the
    # module docstring -- cm4_routing_measure.py:62 shipped that exact bug.
    E = prepare(emb, mu, EXPECT_DIM, "meanproj")

    rows64 = []
    for cid, _name in CLASS_ROWS:
        idx = [i for i, c in enumerate(dev) if c["cls"] == cid]
        v = E[idx].mean(axis=0)
        rows64.append(v / np.linalg.norm(v))
    C64 = np.stack(rows64)
    C32 = C64.astype(np.float32)

    # Cast fidelity, PRINTED not baked in. It is a property of this generation run and
    # is not recomputable from the npz, so putting it in the header would create a
    # number that a later --header-only run could silently leave stale.
    cast_delta = float(np.abs(C64 - C32.astype(np.float64)).max())
    a64 = (E @ C64.T).argmax(axis=1)
    a32 = (E @ C32.astype(np.float64).T).argmax(axis=1)
    flips = int((a64 != a32).sum())
    print("float64 -> float32 cast: max element delta %.3e; nearest-centroid decisions "
          "changed on DEV: %d/%d" % (cast_delta, flips, len(dev)))

    np.savez(NPZ,
             centroids=C32,
             counts=np.asarray(counts, dtype=np.int32),
             suite_sha256=np.frombuffer(bytes.fromhex(suite_sha256(SUITE_H)), dtype=np.uint8))
    print("wrote %s (centroids %s %s)" % (NPZ, C32.shape, C32.dtype))


def write_header():
    cents, counts, suite_sha = read_npz(NPZ)
    with open(NPZ, "rb") as f:
        npz_sha = hashlib.sha256(f.read()).hexdigest()
    text = build_header(cents, counts, suite_sha, npz_sha)
    with open(HEADER, "w", newline="\n") as f:
        f.write(text)
    print("wrote %s" % HEADER)
    print("  %d x %d = %d hex float literals from centroids.npy"
          % (EXPECT_N, EXPECT_DIM, EXPECT_N * EXPECT_DIM))
    print("  npz sha256   %s" % npz_sha)
    print("  suite sha256 %s" % suite_sha)


def check():
    if not os.path.exists(NPZ):
        print("FAIL: %s does not exist" % NPZ)
        return 1
    if not os.path.exists(HEADER):
        print("FAIL: %s does not exist - run the generator" % HEADER)
        return 1
    cents, counts, suite_sha = read_npz(NPZ)
    with open(NPZ, "rb") as f:
        npz_sha = hashlib.sha256(f.read()).hexdigest()
    text = build_header(cents, counts, suite_sha, npz_sha)
    with open(HEADER, "r", newline="") as f:
        on_disk = f.read()
    # Compare CONTENT, not line endings: a clone with autocrlf=true would otherwise
    # fail this gate for a reason unrelated to the header being stale, and a
    # staleness failure is supposed to mean "regenerate from the npz".
    if on_disk.replace("\r\n", "\n") != text.replace("\r\n", "\n"):
        print("FAIL: route_centroids.h is STALE - regenerate with gen_route_centroids.py")
        print("  on disk %d chars, regenerated %d chars" % (len(on_disk), len(text)))
        return 1
    live = suite_sha256(SUITE_H)
    print("OK: route_centroids.h matches the npz-generated content "
          "(%d x %d values, npz %s...)" % (EXPECT_N, EXPECT_DIM, npz_sha[:12]))
    # ADVISORY, never a failure. routing_suite.h changing does not make the header
    # stale w.r.t. the npz -- it makes the FROZEN artifact older than the corpus, which
    # is a decision for a human (regenerating is a measurement, not a rebuild).
    if live != suite_sha:
        print("  NOTE: routing_suite.h has changed since these centroids were fitted")
        print("        fitted %s" % suite_sha)
        print("        now    %s" % live)
        print("        The header is NOT stale. Regenerating is a re-MEASUREMENT and")
        print("        invalidates every number taken against the old centroids.")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--check", action="store_true",
                    help="rebuild the header from the npz and fail if the on-disk copy differs")
    ap.add_argument("--header-only", action="store_true",
                    help="re-render the header from the existing npz (no model, no numpy)")
    ap.add_argument("--force", action="store_true",
                    help="allow the FROZEN npz to be overwritten by a fresh embedding run")
    ap.add_argument("--print-xor", action="store_true",
                    help="print the XOR of the npz bit patterns and exit")
    ap.add_argument("--cpu", action="store_true", help="embed on CPU")
    args = ap.parse_args()

    if args.check:
        return check()
    if args.print_xor:
        cents, _c, _s = read_npz(NPZ)
        xor = 0
        for row in cents:
            for v in row:
                xor ^= bits_of(v)
        print("0x%08X" % xor)
        return 0
    if not args.header_only:
        generate(args.force, args.cpu)
    write_header()
    return 0


if __name__ == "__main__":
    sys.exit(main())
