#!/usr/bin/env python3
"""
cm4_veto_parity.py — does the SHIPPED C veto reproduce the MEASURED Python verdict?

MODEL-GATED-LOCAL, not CI (the cm2_floor.py precedent): this needs the 600 MB
Qwen3-Embedding-0.6B and a GPU-ish host. CI gets a compile-only smoke of the driver instead,
so the C side cannot rot; correctness lives here.

THREE GATES, and any failure is a STOP-and-report, never a nudge target.

  1 DECISION EQUALITY over all 206 queries (69 two-sided corpus + 64 DEV + 73 HELDOUT).
    The Python rule that produced the WORTH BUILDING verdict and the C route_veto_should()
    that will ship must agree on EVERY keyword capture. A single mismatch is a finding —
    most likely a float32 tie-flip — and belongs in front of the strategist, not silently
    smoothed over.

  2 VERDICT REPRODUCTION through the C implementation: FP 6, FN 1, HELDOUT 70/73. These are
    the committed cm4_routing_results.json values. If the C path lands anything else the
    build is not the thing that was measured.

  3 SYSFACTS-ONLY INVARIANCE — the one deliberate narrowing vs the measured rule. Arm C as
    measured vetoed SYSFACTS *and* DECLINE captures; the shipped rule vetoes SYSFACTS only,
    because vetoing a genuine DECLINE would hand a no-source metric question ("what is your
    cpu usage?") to the model to FABRICATE — the exact failure route_decline_text() exists
    to prevent, and a class the measurement never priced. That narrowing is only safe if it
    changes NOTHING measured, so this gate runs BOTH rules over all 206 and asserts zero
    outcome differences, and prints how many DECLINE captures were encountered at all.

--sims prints per-query sim_infer / sim_kw / |margin|, which is where the probe-leg queries
come from (they need a margin comfortably above the measured box-vs-host cosine delta).

Run: python3 phase3/scripts/embed/cm4_veto_parity.py [--sims] [--cpu]
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent.parent
sys.path.insert(0, str(HERE))

from cm2_floor import GOLDEN_NPZ, MODEL, prepare          # noqa: E402
import cm4_routing_measure as M                            # noqa: E402

DRIVER_C = HERE / "cm4_veto_driver.c"
ROUTE_VETO_C = REPO / "phase3" / "src" / "ai" / "route_veto.c"
AI_DIR = REPO / "phase3" / "src" / "ai"
CORPUS = HERE / "routing_twosided.json"
SUITE_H = AI_DIR / "routing_suite.h"
RESULTS = HERE / "cm4_routing_results.json"
NPZ = HERE / "route_centroids.npz"

VETO_EXE = "/tmp/cm4_veto_driver.bin"

# The committed arm-C numbers this gate must reproduce THROUGH THE C PATH.
EXPECT_FP, EXPECT_FN, EXPECT_HELDOUT = 6, 1, 70


def build_veto_driver():
    args = ["gcc", "-Wall", "-Werror", "-O2", "-std=c11", "-I", str(AI_DIR),
            str(DRIVER_C), str(ROUTE_VETO_C), "-o", VETO_EXE, "-lm"]
    if not M.USE_WSL:
        subprocess.run(args, check=True)
        return VETO_EXE
    cmd = " ".join(["gcc", "-Wall", "-Werror", "-O2", "-std=c11", "-I", M._wsl_path(AI_DIR),
                    M._wsl_path(DRIVER_C), M._wsl_path(ROUTE_VETO_C), "-o", VETO_EXE, "-lm"])
    subprocess.run(["wsl.exe", "-e", "bash", "-lc", cmd], check=True)
    return VETO_EXE


def c_veto(exe, vecs, kws):
    """Run route_veto_should() over (vector, keyword-class) pairs via the driver.

    Vectors are transported as C99 hex floats: a decimal round-trip could differ in the last
    bit, and this gate exists to detect a genuine disagreement — a transport-induced tie-flip
    would be indistinguishable from one.
    """
    lines = []
    for v, k in zip(vecs, kws):
        lines.append("%d %s" % (k, " ".join(float(x).hex() for x in v)))
    payload = "\n".join(lines) + "\n"
    argv = ["wsl.exe", "-e", "bash", "-lc", exe] if M.USE_WSL else [exe]
    p = subprocess.run(argv, input=payload, capture_output=True, text=True, check=True)
    out = [l.strip() for l in p.stdout.splitlines() if l.strip()]
    if len(out) != len(lines):
        raise SystemExit("driver returned %d verdicts for %d cases: %s"
                         % (len(out), len(lines), p.stderr[:400]))
    return [int(x) for x in out]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sims", action="store_true")
    ap.add_argument("--cpu", action="store_true")
    a = ap.parse_args()

    corpus = json.loads(CORPUS.read_text(encoding="utf-8"))
    conceptual = [c["q"] for c in corpus["conceptual"]]
    genuine = [c["q"] for c in corpus["genuine"]]
    dev, heldout = M.parse_suite(SUITE_H)

    groups = [("corpus_conceptual", conceptual), ("corpus_genuine", genuine),
              ("dev", [c["q"] for c in dev]), ("heldout", [c["q"] for c in heldout])]
    all_q = [q for _, qs in groups for q in qs]
    print("parity set: %d queries (%s)"
          % (len(all_q), ", ".join("%s %d" % (n, len(qs)) for n, qs in groups)))

    # ---- keyword classes, from the REAL route.c ----
    kw_exe = M.build_driver()
    kw = {n: M.keyword_classify(kw_exe, qs) for n, qs in groups}
    kw_all = [c for n, _ in groups for c in kw[n]]

    # ---- embeddings through the deployed pipeline, imported not reimplemented ----
    from sentence_transformers import SentenceTransformer
    mu = np.load(GOLDEN_NPZ)["mu"].astype(np.float64)
    print("loading %s ..." % MODEL)
    m = SentenceTransformer(MODEL, device="cpu" if a.cpu else None)
    raw = np.asarray(m.encode(all_q, normalize_embeddings=True, batch_size=16,
                              show_progress_bar=False), dtype=np.float64)
    E = prepare(raw, mu, 128, "meanproj")     # STRING sentinel — a bool silently skips it
    E32 = E.astype(np.float32)                # what the box actually holds

    # ---- centroids: the FROZEN npz the C header was generated from ----
    C = np.load(NPZ)["centroids"].astype(np.float64)
    assert C.shape == (3, 128), C.shape

    sim = E32.astype(np.float64) @ C.T        # (N,3): columns are INFER/SYSFACTS/DECLINE

    def py_rule(i, k, sysfacts_only):
        """The measurement's arm_c. sysfacts_only=False is the rule AS MEASURED."""
        if k == 1 or (k == 2 and not sysfacts_only):
            return 1 if sim[i, 0] > sim[i, k] else 0
        return 0

    py_ship = [py_rule(i, k, True) for i, k in enumerate(kw_all)]
    py_meas = [py_rule(i, k, False) for i, k in enumerate(kw_all)]

    # ---- GATE 1: C vs Python, all 206 ----
    exe = build_veto_driver()
    c_ship = c_veto(exe, E32, kw_all)
    mism = [i for i in range(len(all_q)) if c_ship[i] != py_ship[i]]
    print()
    print("GATE 1 decision equality (C vs Python, %d queries): %s"
          % (len(all_q), "PASS" if not mism else "FAIL"))
    for i in mism:
        print("  MISMATCH %r kw=%d py=%d c=%d sim_infer=%.9f sim_kw=%.9f"
              % (all_q[i], kw_all[i], py_ship[i], c_ship[i], sim[i, 0], sim[i, kw_all[i]]))
    if mism:
        raise SystemExit("STOP: C and Python disagree — a finding, not a nudge target.")

    # ---- GATE 2: reproduce the committed arm-C verdict THROUGH C ----
    off, dec = {}, 0
    for n, qs in groups:
        off[n] = dec
        dec += len(qs)

    def final(n, i):
        j = off[n] + i
        return 0 if c_ship[j] else kw[n][i]

    fp = sum(1 for i in range(len(conceptual)) if final("corpus_conceptual", i) != 0)
    fn = sum(1 for i in range(len(genuine)) if final("corpus_genuine", i) != 1)
    hok = sum(1 for i, c in enumerate(heldout) if final("heldout", i) == c["cls"])
    ok2 = (fp == EXPECT_FP and fn == EXPECT_FN and hok == EXPECT_HELDOUT)
    print("GATE 2 verdict through C: FP %d (want %d) | FN %d (want %d) | HELDOUT %d/%d (want %d) -> %s"
          % (fp, EXPECT_FP, fn, EXPECT_FN, hok, len(heldout), EXPECT_HELDOUT,
             "PASS" if ok2 else "FAIL"))
    if not ok2:
        raise SystemExit("STOP: the C path is not the configuration that was measured.")

    # ---- GATE 3: SYSFACTS-only == the measured rule, on this data ----
    diff = [i for i in range(len(all_q)) if py_ship[i] != py_meas[i]]
    n_decl = sum(1 for k in kw_all if k == 2)
    decl_vetoed = sum(1 for i, k in enumerate(kw_all) if k == 2 and py_meas[i] == 1)
    ok3 = not diff
    print("GATE 3 SYSFACTS-only invariance: %d DECLINE captures encountered, the MEASURED rule "
          "would have vetoed %d of them; outcome differences: %d -> %s"
          % (n_decl, decl_vetoed, len(diff), "PASS" if ok3 else "FAIL"))
    for i in diff:
        print("  DIFFERS %r kw=%d shipped=%d measured=%d" % (all_q[i], kw_all[i],
                                                             py_ship[i], py_meas[i]))
    if not ok3:
        raise SystemExit("STOP: the SYSFACTS-only narrowing is NOT measured-invariant.")

    # ---- --sims: margins, for choosing probe queries ----
    if a.sims:
        print()
        print("%-9s %-11s %-11s %-9s %s" % ("kw", "sim_infer", "sim_kw", "margin", "query"))
        rows = []
        for i, q in enumerate(all_q):
            k = kw_all[i]
            if k == 0:
                continue
            rows.append((abs(sim[i, 0] - sim[i, k]), sim[i, 0], sim[i, k], k, c_ship[i], q))
        for mg, si, sk, k, fired, q in sorted(rows, reverse=True):
            print("%-9s %-11.6f %-11.6f %-9.6f %s%s"
                  % (M.CLS_NAME[k], si, sk, mg, "FIRED " if fired else "held  ", q))

    print()
    print("ALL THREE GATES PASS — the shipped C rule is the measured rule.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
