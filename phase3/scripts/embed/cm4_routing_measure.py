#!/usr/bin/env python3
"""
cm4_routing_measure.py — C/M4: can the embedder fix the routing class keyword cannot?

MEASURE ONLY. Touches no box code, changes no route.c rule, and decides nothing on its
own: the verdict bands are PRE-REGISTERED in PROMPT-CM4 §5 and applied verbatim at the end.

THE QUESTION IS NARROW. The embedder clusters intent at 83-93% (C/M0 FINDINGS §3) while the
deployed keyword router measures 95.89% held-out, so a straight SWAP is a measured downgrade.
The only live question is whether the embedder can kill the bare-word FALSE-POSITIVE class
(PHASE_6_GOAL_6-6_ROUTING.md §9) WITHOUT reintroducing false negatives or losing held-out.

THREE ARMS
  A baseline  the deployed keyword rules, obtained by LINKING route.c through
              cm4_route_driver.c. Deliberately not a Python mirror: a reimplementation of
              the thing under measurement can drift from it silently, and the entire value
              of this arm is that it IS the deployed behaviour.
  B semantic  nearest-centroid over SYSFACTS / DECLINE / INFER, centroids from
              routing_suite.h's DEV split ONLY. The control that prices a straight swap.
  C hybrid    keyword decides; the embedder may only VETO a SYSFACTS/DECLINE capture when
              the query sits closer to the INFER centroid. It can never CREATE a capture.

ON ARM C's "CAN ONLY VETO" CLAIM (the prompt asked for this to be checked): it holds as
stated -- the veto only ever rewrites SYSFACTS/DECLINE -> INFER, and INFER is the module's
conservative default, so no capture is ever created. But "cannot create a capture" is NOT
"cannot do harm": vetoing a GENUINE status question turns a correct capture into a false
negative, which is exactly the failure mode that killed the keyword anchor (§9.3) and
exactly what the FN band exists to catch. The claim is about captures, not about safety,
and it is asserted at run time below rather than trusted.

PIPELINE: imported from cm2_floor.py, not reimplemented (the C/M2b precedent), so it cannot
drift from the measured 128/meanproj configuration: embed(1024) -> mean-project with the
frozen mu -> truncate 128 -> L2.

Run: python3 phase3/scripts/embed/cm4_routing_measure.py [--cpu]
"""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent.parent
sys.path.insert(0, str(HERE))

from cm2_floor import GOLDEN_NPZ, MODEL, prepare  # noqa: E402  -- the deployed pipeline

SUITE_H = REPO / "phase3" / "src" / "ai" / "routing_suite.h"
CORPUS = HERE / "routing_twosided.json"
RESULTS = HERE / "cm4_routing_results.json"
DRIVER_C = HERE / "cm4_route_driver.c"
ROUTE_C = REPO / "phase3" / "src" / "ai" / "route.c"
AI_DIR = REPO / "phase3" / "src" / "ai"

DIM = 128          # the C/M2-measured storage dimension
PROJ = "meanproj"  # STRING sentinel — prepare() tests == "meanproj"; a bool SILENTLY skips it

CLS = {"ROUTE_INFER": 0, "ROUTE_SYSFACTS": 1, "ROUTE_DECLINE": 2}
CLS_NAME = {0: "INFER", 1: "SYSFACTS", 2: "DECLINE"}

# Concept-word / self-reference markers, used ONLY for the §6 blind-spot audit of the
# HELDOUT split. They classify nothing and feed no arm.
SELF_WORDS = {"you", "your", "you're", "youre", "we", "our", "us", "i", "me", "my", "this", "it"}
CONCEPT_WORDS = {"a", "an", "does", "do", "did", "explain", "why", "mean", "means",
                 "to", "with", "causes", "cause", "how"}


def parse_suite(path):
    """Extract ROUTING_DEV / ROUTING_HELDOUT from routing_suite.h.

    Read-only: routing_suite.h is never modified (C/M4 is measure-only, and the header's
    own rule forbids editing HELDOUT to make anything pass).
    """
    txt = path.read_text(encoding="utf-8", errors="replace")
    out = {}
    for name in ("ROUTING_DEV", "ROUTING_HELDOUT"):
        m = re.search(re.escape(name) + r"\[\]\s*=\s*\{(.*?)\n\};", txt, re.S)
        if not m:
            raise SystemExit("could not locate %s in %s" % (name, path))
        body = m.group(1)
        cases = []
        for cm in re.finditer(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*(ROUTE_\w+)\s*,\s*(\w+)\s*\}', body):
            q = cm.group(1).encode().decode("unicode_escape")
            cases.append({"q": q, "cls": CLS[cm.group(2)], "field": cm.group(3)})
        out[name] = cases
    return out["ROUTING_DEV"], out["ROUTING_HELDOUT"]


def _have(cmd):
    from shutil import which
    return which(cmd) is not None


def _wsl_path(p):
    """C:\\x\\y -> /mnt/c/x/y. Only needed on the Windows dev host."""
    s = str(Path(p).resolve())
    return "/mnt/%s%s" % (s[0].lower(), s[2:].replace("\\", "/"))


# The dev host splits the toolchain: the CUDA/sentence-transformers Python is Windows-native
# while gcc lives in WSL. Rather than mirror route.c in Python to dodge that (which is the
# one thing this arm must not do), bridge to WSL for the C half only. On CI/Linux both
# branches collapse to the same direct invocation.
USE_WSL = not _have("gcc") and _have("wsl.exe")
EXE = "/tmp/cm4_route_driver.bin"


def build_driver():
    args = ["gcc", "-Wall", "-Werror", "-O2", "-std=c11", "-I", str(AI_DIR),
            str(DRIVER_C), str(ROUTE_C), "-o", EXE]
    if not USE_WSL:
        subprocess.run(args, check=True)
        return EXE
    cmd = " ".join(["gcc", "-Wall", "-Werror", "-O2", "-std=c11", "-I", _wsl_path(AI_DIR),
                    _wsl_path(DRIVER_C), _wsl_path(ROUTE_C), "-o", EXE])
    subprocess.run(["wsl.exe", "-e", "bash", "-lc", cmd], check=True)
    return EXE


def keyword_classify(exe, queries):
    """Arm A: the REAL route_classify, one line in / one line out.

    A blank query would collapse two output lines into a length mismatch, so the corpus is
    checked for empties up front rather than silently mis-aligning the arms.
    """
    assert all(q.strip() for q in queries), "empty query would desynchronise driver output"
    payload = "\n".join(queries) + "\n"
    argv = ["wsl.exe", "-e", "bash", "-lc", exe] if USE_WSL else [exe]
    p = subprocess.run(argv, input=payload, capture_output=True, text=True, check=True)
    lines = [l for l in p.stdout.splitlines() if l.strip()]
    if len(lines) != len(queries):
        raise SystemExit("driver returned %d lines for %d queries" % (len(lines), len(queries)))
    return [int(l.split()[0]) for l in lines]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cpu", action="store_true")
    a = ap.parse_args()

    corpus = json.loads(CORPUS.read_text(encoding="utf-8"))
    conceptual = [c["q"] for c in corpus["conceptual"]]
    genuine = [c["q"] for c in corpus["genuine"]]
    dev, heldout = parse_suite(SUITE_H)

    print("corpus : %d conceptual (expect INFER) + %d genuine (expect SYSFACTS)"
          % (len(conceptual), len(genuine)))
    print("suite  : DEV %d, HELDOUT %d" % (len(dev), len(heldout)))

    # ---------------- Arm A: the deployed classifier, linked not mirrored ----------------
    exe = build_driver()
    kw_con = keyword_classify(exe, conceptual)
    kw_gen = keyword_classify(exe, genuine)
    kw_held = keyword_classify(exe, [c["q"] for c in heldout])
    kw_dev = keyword_classify(exe, [c["q"] for c in dev])

    # ---------------- embeddings ----------------
    from sentence_transformers import SentenceTransformer
    mu = np.load(GOLDEN_NPZ)["mu"].astype(np.float64)
    dev_arg = "cpu" if a.cpu else None
    print("loading %s ..." % MODEL)
    m = SentenceTransformer(MODEL, device=dev_arg)

    def enc(t):
        # sym:none — no prefix, exactly as cm2_floor / C/M0.5 measured.
        return np.asarray(m.encode(t, normalize_embeddings=True, batch_size=16,
                                   show_progress_bar=False), dtype=np.float64)

    def pipe(texts):
        return prepare(enc(texts), mu, DIM, PROJ)

    dev_txt = [c["q"] for c in dev]
    E_dev = pipe(dev_txt)
    E_con = pipe(conceptual)
    E_gen = pipe(genuine)
    E_held = pipe([c["q"] for c in heldout])

    # ---------------- centroids: DEV SPLIT ONLY ----------------
    # HELDOUT is never used to build or tune anything -- routing_suite.h's own header rule.
    cents = {}
    for cid in (0, 1, 2):
        idx = [i for i, c in enumerate(dev) if c["cls"] == cid]
        if not idx:
            continue
        v = E_dev[idx].mean(axis=0)
        cents[cid] = v / np.linalg.norm(v)
    print("centroids from DEV only: " +
          ", ".join("%s n=%d" % (CLS_NAME[c], sum(1 for x in dev if x["cls"] == c))
                    for c in sorted(cents)))
    C = np.stack([cents[c] for c in sorted(cents)])
    C_ids = sorted(cents)

    def sims(E):
        return E @ C.T

    def arm_b(E):
        s = sims(E)
        return [C_ids[i] for i in s.argmax(axis=1)]

    def arm_c(E, kw):
        """Hybrid: keyword decides; the embedder may only VETO a capture.

        The rule is PARAMETER-FREE on purpose. A margin threshold would be a knob, and any
        knob tuned to make the numbers work is tuning -- against HELDOUT it would be
        forbidden outright, and against the two-sided corpus it would quietly overfit the
        very set the verdict is read from.
        """
        infer_col = C_ids.index(0)
        s = sims(E)
        out, vetoed = [], []
        for i, k in enumerate(kw):
            if k in (1, 2) and s[i, infer_col] > s[i, C_ids.index(k)]:
                out.append(0)
                vetoed.append(i)
            else:
                out.append(k)      # never upgrades INFER -> a capture
        return out, vetoed

    b_con, b_gen, b_held = arm_b(E_con), arm_b(E_gen), arm_b(E_held)
    c_con, c_con_v = arm_c(E_con, kw_con)
    c_gen, c_gen_v = arm_c(E_gen, kw_gen)
    c_held, _ = arm_c(E_held, kw_held)

    # ---- assert arm C's structural claim rather than trusting the prompt's wording ----
    created = [i for i, (k, c) in enumerate(zip(kw_con + kw_gen, c_con + c_gen))
               if k == 0 and c != 0]
    assert not created, "arm C created a capture from INFER — the veto-only claim is FALSE"

    # ---------------- scoring ----------------
    def score(con_pred, gen_pred, held_pred):
        fp = [i for i, p in enumerate(con_pred) if p != 0]          # conceptual captured
        fn = [i for i, p in enumerate(gen_pred) if p != 1]          # genuine not SYSFACTS
        hok = sum(1 for c, p in zip(heldout, held_pred) if c["cls"] == p)
        return fp, fn, hok

    arms = {}
    for name, (cp, gp, hp) in (("A_keyword_baseline", (kw_con, kw_gen, kw_held)),
                               ("B_pure_semantic", (b_con, b_gen, b_held)),
                               ("C_hybrid_veto", (c_con, c_gen, c_held))):
        fp, fn, hok = score(cp, gp, hp)
        arms[name] = {
            "false_positives": len(fp), "fp_of": len(conceptual),
            "false_negatives": len(fn), "fn_of": len(genuine),
            "total_wrong": len(fp) + len(fn),
            "heldout_correct": hok, "heldout_of": len(heldout),
            "heldout_pct": round(100.0 * hok / len(heldout), 2),
            "fp_questions": [{"q": conceptual[i], "routed": CLS_NAME[cp[i]]} for i in fp],
            "fn_questions": [{"q": genuine[i], "routed": CLS_NAME[gp[i]]} for i in fn],
            "heldout_misses": [{"q": heldout[i]["q"], "want": CLS_NAME[heldout[i]["cls"]],
                                "got": CLS_NAME[hp[i]]}
                               for i in range(len(heldout)) if heldout[i]["cls"] != hp[i]],
        }

    dev_ok = sum(1 for c, p in zip(dev, kw_dev) if c["cls"] == p)

    # ---- SELECTION-BIAS AUDIT: this corpus is 55/69 author-written --------------------
    # §9 was docs-only, so most of these questions were constructed by the same session that
    # is now scoring them. If arm C's improvement were concentrated in the REBUILT half, the
    # result would be an artefact of how the questions were phrased rather than a property of
    # the embedder. The 14 questions quoted verbatim from §9 are the only unbiased anchor
    # available, so they are scored separately.
    con_src = [c["source"] for c in corpus["conceptual"]]
    gen_src = [c["source"] for c in corpus["genuine"]]

    def by_source(cp, gp):
        o = {}
        for tag in ("recovered", "rebuilt"):
            ci = [i for i, s in enumerate(con_src) if s == tag]
            gi = [i for i, s in enumerate(gen_src) if s == tag]
            o[tag] = {
                "conceptual_n": len(ci),
                "false_positives": sum(1 for i in ci if cp[i] != 0),
                "genuine_n": len(gi),
                "false_negatives": sum(1 for i in gi if gp[i] != 1),
            }
        return o

    bias = {"A_keyword_baseline": by_source(kw_con, kw_gen),
            "C_hybrid_veto": by_source(c_con, c_gen)}

    # ---- Did the veto fire on HELDOUT AT ALL? ----------------------------------------
    # Arm C's HELDOUT score being identical to baseline can mean two very different things:
    # the veto fired and was right, or the veto never fired there. Only the first is
    # evidence; the second is silence. Count it rather than infer it.
    held_vetoed = [i for i in range(len(heldout))
                   if kw_held[i] in (1, 2) and c_held[i] == 0]

    # ---------------- §6 blind-spot audit of HELDOUT ----------------
    # The suite's SYSFACTS positives were authored before this defect was known. If none of
    # them sit in the (no-self AND concept-word) quadrant, a green HELDOUT proves less than
    # it appears to -- it would simply not contain the shape an embedder-veto decides.
    quad = []
    for c in heldout:
        if c["cls"] != 1:
            continue
        w = set(re.findall(r"[a-z0-9']+", c["q"].lower()))
        if not (w & SELF_WORDS) and (w & CONCEPT_WORDS):
            quad.append(c["q"])
    held_sys = [c for c in heldout if c["cls"] == 1]
    quad_idx = [i for i, c in enumerate(heldout) if c["q"] in set(quad)]
    quad_survived = [heldout[i]["q"] for i in quad_idx if c_held[i] == heldout[i]["cls"]]
    blind = {
        "heldout_sysfacts_positives": len(held_sys),
        "in_no_self_AND_concept_word_quadrant": len(quad),
        "quadrant_questions": quad,
        "quadrant_survived_arm_c": len(quad_survived),
        "arm_c_vetoes_fired_on_heldout": len(held_vetoed),
        "heldout_vetoed_questions": [heldout[i]["q"] for i in held_vetoed],
        "verdict": ("HELDOUT DOES exercise the quadrant an embedder-veto decides"
                    if quad else
                    "HELDOUT DOES NOT exercise that quadrant — a green HELDOUT here proves "
                    "LESS than it appears to, exactly as it did at 95.52% and 96.47%"),
        "what_the_heldout_score_actually_proves":
            ("Arm C's HELDOUT is IDENTICAL to baseline because the veto fired on %d HELDOUT "
             "case(s). With zero firings that is evidence of NO HARM, not evidence the veto "
             "was validated on held-out data — the veto was simply never exercised there. "
             "Read it as 'costs nothing on HELDOUT', never as 'HELDOUT confirms the veto'."
             % len(held_vetoed)),
    }

    # ---------------- PRE-REGISTERED verdict (PROMPT-CM4 §5), applied verbatim ----------
    def verdict(a_):
        fp, fn, h = a_["false_positives"], a_["false_negatives"], a_["heldout_pct"]
        if fp <= 8 and fn <= 2 and h >= 95.0:
            return "WORTH BUILDING"
        if 9 <= fp <= 16 and fn <= 2 and h >= 95.0:
            return "REPORT, DO NOT DECIDE"
        return "CLOSED"

    for n, v in arms.items():
        if n != "A_keyword_baseline":
            v["verdict"] = verdict(v)

    out = {
        "_comment": "C/M4 routing measurement. MEASURE ONLY — no route.c change was made and "
                    "none is implied. Verdict bands are pre-registered in PROMPT-CM4 §5 and "
                    "applied verbatim; they were fixed before any number was seen.",
        "model": MODEL, "dim": DIM, "projection": "mean-project (frozen mu) + L2",
        "corpus": {"file": CORPUS.name, "conceptual": len(conceptual), "genuine": len(genuine),
                   "recovered_verbatim_from_doc_s9": corpus["_recovered_count"],
                   "rebuilt_under_same_rule": corpus["_rebuilt_count"],
                   "note": "PHASE_6_GOAL_6-6_ROUTING.md §9 was DOCS-ONLY and enumerates only "
                           "~14 of its 69 questions, so this corpus is PARTLY REBUILT and is "
                           "NOT byte-comparable to §9's recorded 32/69 baseline. Arm A is "
                           "re-measured here to give this corpus its own baseline."},
        "suite": {"dev": len(dev), "heldout": len(heldout),
                  "keyword_dev_correct": dev_ok,
                  "centroid_source": "DEV split only (HELDOUT never tuned against)"},
        "arms": arms,
        "arm_c_veto_only_claim": {
            "asserted_at_runtime": True,
            "captures_created_from_infer": len(created),
            "note": "Holds as stated: the veto only rewrites SYSFACTS/DECLINE -> INFER, and "
                    "INFER is the conservative default, so no capture is ever created. But "
                    "this is a claim about CAPTURES, not about harm — vetoing a genuine "
                    "status question converts a correct capture into a FALSE NEGATIVE, which "
                    "is precisely what killed the keyword anchor and what the FN band catches.",
            "arm_c_vetoes_on_conceptual": len(c_con_v),
            "arm_c_vetoes_on_genuine": len(c_gen_v),
        },
        "heldout_blind_spot_audit": blind,
        "selection_bias_audit": {
            "why": "55 of the 69 questions were written by the session that scored them "
                   "(§9 was docs-only). If arm C's gain were concentrated in the REBUILT "
                   "half it would be an artefact of phrasing, not a property of the "
                   "embedder. The 14 questions quoted verbatim from §9 are the only "
                   "unbiased anchor available.",
            "by_source": bias,
        },
    }
    RESULTS.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")

    # ---------------- report ----------------
    print()
    print("%-22s %-14s %-14s %-16s %s" % ("arm", "false POS", "false NEG", "HELDOUT", "verdict"))
    for n, v in arms.items():
        print("%-22s %-14s %-14s %-16s %s" % (
            n, "%d/%d" % (v["false_positives"], v["fp_of"]),
            "%d/%d" % (v["false_negatives"], v["fn_of"]),
            "%d/%d = %.2f%%" % (v["heldout_correct"], v["heldout_of"], v["heldout_pct"]),
            v.get("verdict", "(baseline)")))
    print()
    print("keyword DEV: %d/%d" % (dev_ok, len(dev)))
    print("HELDOUT blind-spot audit: %d/%d SYSFACTS positives in the (no-self AND concept-word) "
          "quadrant" % (blind["in_no_self_AND_concept_word_quadrant"],
                        blind["heldout_sysfacts_positives"]))
    print("  -> " + blind["verdict"])
    print("  -> arm C vetoes that fired on HELDOUT: %d  (quadrant cases surviving: %d/%d)"
          % (blind["arm_c_vetoes_fired_on_heldout"], blind["quadrant_survived_arm_c"], len(quad)))
    print()
    print("selection-bias audit (recovered = quoted from §9, rebuilt = written here):")
    for arm in ("A_keyword_baseline", "C_hybrid_veto"):
        b = bias[arm]
        print("  %-20s recovered FP %d/%d, FN %d/%d | rebuilt FP %d/%d, FN %d/%d" % (
            arm,
            b["recovered"]["false_positives"], b["recovered"]["conceptual_n"],
            b["recovered"]["false_negatives"], b["recovered"]["genuine_n"],
            b["rebuilt"]["false_positives"], b["rebuilt"]["conceptual_n"],
            b["rebuilt"]["false_negatives"], b["rebuilt"]["genuine_n"]))
    print()
    print("wrote %s" % RESULTS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
