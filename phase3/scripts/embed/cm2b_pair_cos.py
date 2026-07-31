#!/usr/bin/env python3
"""
C/M2b Leg A (host half): per-pair cosines for the MEASURED recall pairs.

WHY THIS EXISTS
---------------
cm2_floor_results.json records only AGGREGATES (recall@1, margins, the floor sweep).
There is no per-pair cosine anywhere, so "does the box reproduce the measurement?"
could not be asked, let alone answered. This emits the per-pair host numbers the box
probe is compared against.

It also closes a real hole in the arc: every parity check so far stops at the RAW
1024 vector (C/M1a Stage 2, T2's G2). The projection, truncation, renormalisation
and cosine ON THE BOX have never been compared to the host measurement that produced
the 0.55 floor. A box-side pipeline bug would move cosines and no existing gate could
see it.

PIPELINE FIDELITY IS NOT REIMPLEMENTED — IT IS IMPORTED. l2, mean_project and prepare
come from cm2_floor itself, so this cannot drift from the script that produced the
deployed floor. Reimplementing them here would make an agreement result meaningless:
two copies of the same bug agree perfectly.

Run:  py -3 phase3/scripts/embed/cm2b_pair_cos.py            # GPU if available
      py -3 phase3/scripts/embed/cm2b_pair_cos.py --cpu
Out:  phase3/scripts/embed/cm2b_pair_cos.json
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

# The pipeline, imported rather than copied — see the docstring.
from cm2_floor import MODEL, RECALL_JSON, GOLDEN_NPZ, prepare  # noqa: E402

OUT_JSON = HERE / "cm2b_pair_cos.json"

DIM = 128          # G3_SEM_DIM_DEFAULT — the deployed store/selector dimension
PROJ = "meanproj"  # the deployed projection
FLOOR = 0.55       # G3_SEM_FLOOR_DEFAULT — NOT a knob here; this script only measures

# The pair invented for the first G2 attempt, carried as a LABELLED EXTRA. It is not
# part of the measured set and must never be counted in the fire rate; knowing where
# it sits relative to the measured distribution is the point.
EXTRA = {
    "stored": "what is a page fault",
    "later": "explain what happens when a memory page is not resident",
    "note": "INVENTED for the first G2 attempt - not in cm0_recall_set.json, excluded from the rate",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cpu", action="store_true")
    a = ap.parse_args()

    rs = json.loads(RECALL_JSON.read_text(encoding="utf-8"))
    pairs = rs["distinct_positives"]
    mu = np.load(GOLDEN_NPZ)["mu"].astype(np.float64)

    stored_txt = [p["stored"] for p in pairs] + [EXTRA["stored"]]
    later_txt = [p["later"] for p in pairs] + [EXTRA["later"]]

    from sentence_transformers import SentenceTransformer
    dev = "cpu" if a.cpu else None
    print(f"loading {MODEL} ...")
    m = SentenceTransformer(MODEL, device=dev)

    def enc(t):
        return np.asarray(m.encode(t, normalize_embeddings=True, batch_size=16,
                                   show_progress_bar=False), dtype=np.float64)

    print(f"embedding {len(stored_txt)} stored + {len(later_txt)} later (sym:none) ...")
    S_raw, L_raw = enc(stored_txt), enc(later_txt)
    assert S_raw.shape[1] == 1024, f"expected 1024-dim, got {S_raw.shape[1]}"

    S = prepare(S_raw, mu, DIM, PROJ)
    L = prepare(L_raw, mu, DIM, PROJ)

    rows = []
    for i in range(len(pairs)):
        c = float(np.dot(S[i], L[i]))
        rows.append({"i": i, "stored": pairs[i]["stored"], "later": pairs[i]["later"],
                     "cos": c, "clears_floor": bool(c >= FLOOR)})

    ec = float(np.dot(S[-1], L[-1]))
    extra = dict(EXTRA); extra["cos"] = ec; extra["clears_floor"] = bool(ec >= FLOOR)

    fire = sum(1 for r in rows if r["clears_floor"])
    cs = sorted(r["cos"] for r in rows)

    print(f"\n{'#':>3} {'cos':>8}  {'>=0.55':>6}  stored -> later")
    for r in rows:
        print(f"{r['i']:>3} {r['cos']:>8.4f}  {'YES' if r['clears_floor'] else '  -':>6}  "
              f"{r['stored'][:34]:34s} -> {r['later'][:44]}")
    print(f"\nEXTRA (invented, NOT counted): {ec:.4f} "
          f"{'>= floor' if extra['clears_floor'] else '< floor'}  -- {EXTRA['later']}")

    print(f"\nfire rate at floor {FLOOR}: {fire}/{len(rows)} = {fire/len(rows)*100:.1f}%")
    print(f"cos min={cs[0]:.4f} median={cs[len(cs)//2]:.4f} max={cs[-1]:.4f}")

    OUT_JSON.write_text(json.dumps({
        "_comment": "C/M2b Leg A host reference. Per-pair cosines for the DEPLOYED config "
                    "(dim 128, mean-projected, floor 0.55). Pipeline imported from cm2_floor.py "
                    "so it cannot drift. The box probe is compared against these.",
        "model": MODEL, "dim": DIM, "projection": PROJ, "floor": FLOOR,
        "n_pairs": len(rows), "fire_at_floor": fire,
        "cos_min": cs[0], "cos_median": cs[len(cs)//2], "cos_max": cs[-1],
        "pairs": rows, "extra_invented_pair": extra,
    }, indent=2), encoding="utf-8")
    print(f"\nwrote {OUT_JSON}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
