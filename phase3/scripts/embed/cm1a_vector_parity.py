#!/usr/bin/env python3
"""cm1a_vector_parity.py -- Phase C / C/M1a Stage 2 vector-parity GATE (host, model-gated-local).

Compares C-engine embeddings produced by embed_vector_probe.c (raw little-endian float32,
n_probe x dim) against the goldens in this directory.

WHY THERE ARE THREE COMPARISONS
-------------------------------
The original C/M1a spec had ONE tight gate -- GATE 2, "engine-vs-engine at the same weight quant
=> cosine >= 1-1e-4 => a gap is a PORT bug". Stage 2 MEASURED that premise and found it
MIS-SPECIFIED, not merely unmet:

  llama.cpp quantizes its ACTIVATIONS (to Q8_x) so it can use an integer dot product. JARVIS's
  qdot_row dequantizes the Q8_0 WEIGHT and dots it against F32 ACTIVATIONS (`const float *x`).
  So at the SAME weight quant the two engines still do different arithmetic, and bit-level
  agreement at 1e-4 is unreachable BY CONSTRUCTION -- no port fix can close it.

Deleting GATE 2 would delete the silent-failure guard: GATE 3's tolerance (1.27e-3) is ~3.4x the
observed C-vs-F32 gap, leaving a band in which a SUBTLE port bug (wrong rope_theta, eps mismatch,
misapplied q_norm/k_norm) passes unnoticed. So GATE 2 is REPLACED, not dropped, by:

  GATE 2' -- SAME-PRECISION port gate. Run OUR engine on an F32 GGUF converted from the same HF
  checkpoint and compare to the ST F32 golden. Both sides then hold F32 weights AND F32
  activations, so the activation-quant confound is gone. The Qwen3-Embedding-0.6B checkpoint is
  BF16 on disk, and BF16 -> F32 is a LOSSLESS widening, so the converted F32 GGUF carries
  BIT-IDENTICAL weight VALUES to what PyTorch computed with: the weight-delta term is ZERO and
  any residual is pure arithmetic (CPU AVX2 order vs CUDA, our RMSNorm/RoPE/softmax FP details).

INTERPRETATION BANDS -- fixed in advance so the verdict cannot be re-based after seeing numbers:
  (a) 1-cos <= 1e-5        -> port proven to numerical noise. GATE 2' PASS, decisive.
  (a) 1e-5 < 1-cos <= 1e-4 -> port correct; residual = dtype/arithmetic order. GATE 2' PASS.
  (a) 1-cos > 1e-4         -> a residual PORT discrepancy EXISTS. Do NOT widen the band. Report,
                              stop, hunt it.

REPORTED COMPARISONS
  (a) C-F32   vs golden_vectors.npz (ST F32)  <- GATE 2', THE port-correctness gate
  (b) C-F32   vs C-Q8_0                       <- our OWN pure weight-quant delta; expected to sit
                                                 at the same ORDER as llama.cpp's own F32->Q8_0
                                                 gap (golden_meta measured_quant_floor.gap_max)
  (c) C-Q8_0  vs gguf_golden.npz (llama.cpp)  <- the cross-engine DIAGNOSTIC (reported, not gating)
  GATE 3      C-Q8_0 vs golden_vectors.npz    <- F32 end-to-end sanity at the MEASURED floor

"gap" is 1 - cosine throughout -- the exact metric cm1a_golden.py measured the floor with. All
vectors are L2-normalized, so cosine == dot. Max per-element abs diff is also reported (cosine
alone can hide a scale/offset under FP-associativity).

Exit 0 only if GATE 2' and GATE 3 both pass. MEASURE-FIRST: every per-probe number prints before
any verdict.

Run:
  py=$USERPROFILE/miniconda3/python.exe
  $py phase3/scripts/embed/cm1a_vector_parity.py <c_q8_0.bin> --c-f32 <c_f32.bin>
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent

# Pre-registered bands (see the module docstring). Do NOT widen these to fit a result.
GATE2P_NOISE = 1e-5    # <= this: port proven to numerical noise
GATE2P_TOL = 1e-4      # <= this: port correct, residual = arithmetic order
GATE2_LEGACY_TOL = 1e-4  # the ORIGINAL cross-engine target, retained ONLY to report the diagnostic


def l2(v):
    v = np.asarray(v, dtype=np.float32)
    return v / (np.linalg.norm(v) + 1e-9)


def load_c_vectors(path, n, dim):
    raw = np.fromfile(path, dtype="<f4")
    if raw.size != n * dim:
        print(f"FATAL: {path} size {raw.size} != {n}*{dim}={n * dim} "
              f"(rebuild/rerun embed_vector_probe.c?)", file=sys.stderr)
        return None
    return raw.reshape(n, dim)


def pairwise(A, B):
    """Return (cos, gap=1-cos, maxabs) per row."""
    out = []
    for i in range(A.shape[0]):
        a, b = l2(A[i]), l2(B[i])
        cos = float(np.dot(a, b))
        out.append((cos, 1.0 - cos, float(np.max(np.abs(a - b)))))
    return out


def report(title, rows, texts, tol=None, tol_label=""):
    print(f"\n== {title} ==")
    if tol is not None:
        print(f"   threshold: 1-cos <= {tol:.2e}  {tol_label}")
    print("   #   cosine      1-cos     maxabs   %s probe" % ("verdict" if tol else "       "))
    worst = 0.0
    ok = True
    for i, (cos, gap, maxabs) in enumerate(rows):
        worst = max(worst, gap)
        mark = ""
        if tol is not None:
            good = gap <= tol
            ok &= good
            mark = "OK " if good else "XX "
        print("  %2d  %9.6f  %.2e  %.2e  %-7s %s" % (i, cos, gap, maxabs, mark, texts[i][:38]))
    print("   worst (max 1-cos): %.2e   mean: %.2e"
          % (worst, sum(r[1] for r in rows) / len(rows)))
    return worst, ok


def main():
    ap = argparse.ArgumentParser(description="C/M1a Stage 2 vector-parity gate")
    ap.add_argument("c_vectors", help="C-engine Q8_0 vectors (raw float32, n_probe x dim)")
    ap.add_argument("--c-f32", dest="c_f32", default=None,
                    help="C-engine F32 vectors (matched-precision run) -- enables GATE 2'")
    args = ap.parse_args()

    meta = json.loads((HERE / "golden_meta.json").read_text(encoding="utf-8"))
    n = int(meta["n_probe"])
    dim = int(meta["dim"])
    f32_tol = float(meta["measured_quant_floor"]["f32_tolerance"])
    llama_quant_gap = float(meta["measured_quant_floor"]["gap_max"])
    texts = list(meta["token_ids"].keys())      # PROBE_TEXTS order (insertion-preserved)
    assert len(texts) == n, (len(texts), n)

    gg = np.load(HERE / "gguf_golden.npz")["vectors_raw"].astype(np.float32)     # L2, llama.cpp Q8_0
    fv = np.load(HERE / "golden_vectors.npz")["vectors_raw"].astype(np.float32)  # L2, ST F32
    if gg.shape != (n, dim) or fv.shape != (n, dim):
        print(f"FATAL: golden shapes {gg.shape}/{fv.shape} != ({n},{dim})", file=sys.stderr)
        return 2

    Cq = load_c_vectors(args.c_vectors, n, dim)
    if Cq is None:
        return 2
    Cf = None
    if args.c_f32:
        Cf = load_c_vectors(args.c_f32, n, dim)
        if Cf is None:
            return 2

    print("== C/M1a Stage 2 VECTOR PARITY (%d probes x %d dim) ==" % (n, dim))
    print("   C Q8_0 vectors: %s" % args.c_vectors)
    print("   C F32  vectors: %s" % (args.c_f32 or "(not supplied -- GATE 2' CANNOT RUN)"))

    # --- GATE 3: F32 end-to-end sanity of the Q8_0 run, at the MEASURED floor ---
    g3_worst, g3_ok = report("GATE 3  C-Q8_0 vs ST-F32 golden (end-to-end sanity)",
                             pairwise(Cq, fv), texts, f32_tol, "(MEASURED quant floor, not guessed)")

    # --- (c) the cross-engine DIAGNOSTIC (reported, NOT gating -- see the docstring) ---
    c_worst, _ = report("(c) DIAGNOSTIC  C-Q8_0 vs llama.cpp-Q8_0 golden [the original GATE 2]",
                        pairwise(Cq, gg), texts)
    c_pass_legacy = c_worst <= GATE2_LEGACY_TOL   # evaluate honestly; report() does not gate when tol=None
    print("   NOTE: the original GATE 2 asked for <= %.0e here. That premise is MIS-SPECIFIED --"
          % GATE2_LEGACY_TOL)
    print("         llama.cpp quantizes ACTIVATIONS, JARVIS keeps them F32, so engine-vs-engine")
    print("         agreement at that level is unreachable by construction. Reported, not gated.")
    print("         legacy <=%.0e would have said: %s" % (GATE2_LEGACY_TOL,
                                                          "PASS" if c_pass_legacy else "FAIL"))

    g2p_worst = g2p_ok = None
    if Cf is not None:
        # --- (a) GATE 2': the real port gate, same precision on both sides ---
        g2p_worst, g2p_ok = report("(a) GATE 2'  C-F32 vs ST-F32 golden [PORT CORRECTNESS]",
                                   pairwise(Cf, fv), texts, GATE2P_TOL,
                                   "(pre-registered band; BF16->F32 is lossless => zero weight delta)")
        # --- (b) our own weight-quant delta ---
        b_worst, _ = report("(b) C-F32 vs C-Q8_0 [our OWN pure weight-quant delta]",
                            pairwise(Cf, Cq), texts)
        print("   llama.cpp's own F32->Q8_0 gap_max (golden_meta) = %.2e -- same ORDER expected."
              % llama_quant_gap)
        ratio = b_worst / llama_quant_gap if llama_quant_gap else float("inf")
        print("   ours / llama.cpp = %.2fx  -> %s" % (
            ratio, "same order, consistent" if 0.1 <= ratio <= 10 else "DIFFERENT ORDER - investigate"))

    # --- verdict ---
    print("\n" + "=" * 78)
    print("GATE 3  (F32 sanity, measured floor %.2e):  %s   worst %.2e"
          % (f32_tol, "PASS" if g3_ok else "FAIL", g3_worst))
    if g2p_ok is None:
        print("GATE 2' (port correctness):                  NOT RUN -- rerun with --c-f32")
        print("\nC/M1a VECTOR PARITY: INCOMPLETE")
        return 2
    band = ("<= 1e-5 NOISE (decisive)" if g2p_worst <= GATE2P_NOISE
            else "1e-5..1e-4 arithmetic-order" if g2p_worst <= GATE2P_TOL
            else "> 1e-4 RESIDUAL PORT DISCREPANCY")
    print("GATE 2' (port correctness, same precision):  %s   worst %.2e  [%s]"
          % ("PASS" if g2p_ok else "FAIL", g2p_worst, band))
    ok = g3_ok and g2p_ok
    print("\nC/M1a VECTOR PARITY: %s" % ("GREEN" if ok else "RED"))
    if not ok:
        print("Do NOT widen the bands. Report and hunt the discrepancy.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
