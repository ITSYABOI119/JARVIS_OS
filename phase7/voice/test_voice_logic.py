#!/usr/bin/env python3
"""Standard-library-only tests for the pure logic of jarvis_voice (Phase 7 goal 8, M0a).

Run: python3 phase7/voice/test_voice_logic.py  -> PASS/FAIL per check, exit non-zero on any FAIL.
No numpy, no torch, no audio library is imported here or by the modules under test at module level;
GPU code is never touched.
"""
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from jarvis_voice.evaluate import eer, far_frr_at, accuracy_at  # noqa: E402
from jarvis_voice.verify import decide, refused_for_length, score, MIN_CLIP_S  # noqa: E402
from jarvis_voice.enroll import build_centroid  # noqa: E402
from jarvis_voice.transcribe import finalize  # noqa: E402

FAILS = 0
CHECKS = 0


def check(name, cond, detail=""):
    global FAILS, CHECKS
    CHECKS += 1
    if cond:
        print(f"PASS {name}")
    else:
        FAILS += 1
        print(f"FAIL {name} {detail}")


def close(a, b, tol=1e-9):
    return abs(a - b) <= tol


# T1 — perfectly separable sets: EER 0 and a threshold strictly between the sets
pos1, neg1 = [0.9, 0.8, 0.7], [0.3, 0.2, 0.1]
e1, t1 = eer(pos1, neg1)
check("T1 eer separable -> 0.0", close(e1, 0.0), f"got {e1}")
check("T1 threshold strictly between the sets", max(neg1) < t1 < min(pos1), f"got {t1}")
check("T1 midpoint of the gap", close(t1, 0.5), f"got {t1}")

# T2 — overlapping sets with a hand-derived answer.
# pos [0.9, 0.8, 0.6, 0.4], neg [0.5, 0.3, 0.2, 0.1]; decision rule score >= t -> owner.
# Candidates (all distinct scores, ascending): 0.1 0.2 0.3 0.4 0.5 0.6 0.8 0.9.
#   t=0.1: FAR=4/4  FRR=0/4        t=0.4: FAR=1/4 (0.5)  FRR=0/4
#   t=0.2: FAR=3/4  FRR=0/4        t=0.5: FAR=1/4 (0.5)  FRR=1/4 (0.4)  <- FAR == FRR here
#   t=0.3: FAR=2/4  FRR=0/4
# The first candidate where FAR <= FRR is t=0.5 with FAR == FRR == 0.25 exactly, so no
# interpolation is needed: EER = 0.25 at threshold 0.5.
pos2, neg2 = [0.9, 0.8, 0.6, 0.4], [0.5, 0.3, 0.2, 0.1]
e2, t2 = eer(pos2, neg2)
check("T2 eer overlapping -> 0.25", close(e2, 0.25), f"got {e2}")
check("T2 threshold -> 0.5", close(t2, 0.5), f"got {t2}")
far2, frr2 = far_frr_at(t2, pos2, neg2)
check("T2 FAR == FRR == 0.25 at the threshold", close(far2, 0.25) and close(frr2, 0.25), f"got {far2},{frr2}")
check("T2 accuracy at threshold == 0.75", close(accuracy_at(t2, pos2, neg2), 0.75))
# a case that needs interpolation: pos [0.9, 0.7, 0.5], neg [0.6, 0.4, 0.1]
#   t=0.1: FAR 1, FRR 0 | t=0.4: FAR 2/3, FRR 0 | t=0.5: FAR 2/3, FRR 0 | t=0.6: FAR 1/3, FRR 1/3 (0.5)
#   -> exact crossing at t=0.6, EER 1/3
e2b, t2b = eer([0.9, 0.7, 0.5], [0.6, 0.4, 0.1])
check("T2b eer -> 1/3 at 0.6", close(e2b, 1 / 3) and close(t2b, 0.6), f"got {e2b},{t2b}")
# pos [0.9, 0.7, 0.3], neg [0.8, 0.2, 0.1]: t=0.3 FAR 1/3 FRR 0; t=0.7 FAR 1/3 FRR 1/3 -> EER 1/3 @ 0.7
e2c, t2c = eer([0.9, 0.7, 0.3], [0.8, 0.2, 0.1])
check("T2c eer -> 1/3 at 0.7", close(e2c, 1 / 3) and close(t2c, 0.7), f"got {e2c},{t2c}")
# interpolation proper: pos [0.9, 0.55], neg [0.6, 0.5]
#   t=0.5: FAR 1, FRR 0 | t=0.55: FAR 1/2, FRR 0 | t=0.6: FAR 1/2, FRR 1/2 -> exact at 0.6, EER 0.5
e2d, t2d = eer([0.9, 0.55], [0.6, 0.5])
check("T2d eer -> 0.5 at 0.6", close(e2d, 0.5) and close(t2d, 0.6), f"got {e2d},{t2d}")
# a crossing between candidates: pos [0.9, 0.8, 0.7, 0.35], neg [0.6, 0.5, 0.4, 0.1]
#   t=0.35: FAR 3/4 FRR 0 | t=0.4: FAR 3/4 FRR 1/4 | t=0.5: FAR 2/4 FRR 1/4 | t=0.6: FAR 1/4 FRR 1/4 -> exact 0.25 @ 0.6
e2e, t2e = eer([0.9, 0.8, 0.7, 0.35], [0.6, 0.5, 0.4, 0.1])
check("T2e eer -> 0.25 at 0.6", close(e2e, 0.25) and close(t2e, 0.6), f"got {e2e},{t2e}")
# genuine interpolation: pos [0.9, 0.8, 0.45], neg [0.7, 0.5, 0.1]
#   t=0.45: FAR 2/3 FRR 0 | t=0.5: FAR 2/3 FRR 1/3 | t=0.7: FAR 1/3 FRR 1/3 -> exact 1/3 @ 0.7
e2f, t2f = eer([0.9, 0.8, 0.45], [0.7, 0.5, 0.1])
check("T2f eer -> 1/3 at 0.7", close(e2f, 1 / 3) and close(t2f, 0.7), f"got {e2f},{t2f}")
# interpolation where FAR jumps below FRR: pos [0.9, 0.8, 0.75, 0.3], neg [0.7, 0.72, 0.2, 0.1]
#   t=0.3: FAR 2/4 FRR 0 | t=0.7: FAR 2/4 FRR 1/4 | t=0.72: FAR 1/4 FRR 1/4 -> exact 0.25 @ 0.72
e2g, t2g = eer([0.9, 0.8, 0.75, 0.3], [0.7, 0.72, 0.2, 0.1])
check("T2g eer -> 0.25 at 0.72", close(e2g, 0.25) and close(t2g, 0.72), f"got {e2g},{t2g}")
# a strict crossing: pos [0.9, 0.8, 0.3], neg [0.6, 0.5, 0.4]
#   t=0.3: FAR 1 FRR 0 | t=0.4: FAR 1 FRR 1/3 | t=0.5: FAR 2/3 FRR 1/3 | t=0.6: FAR 1/3 FRR 1/3 -> exact 1/3 @ 0.6
e2h, t2h = eer([0.9, 0.8, 0.3], [0.6, 0.5, 0.4])
check("T2h eer -> 1/3 at 0.6", close(e2h, 1 / 3) and close(t2h, 0.6), f"got {e2h},{t2h}")
# interpolated: pos [0.9, 0.85, 0.2], neg [0.8, 0.3, 0.25]
#   t=0.2: FAR 1 FRR 0 | t=0.25: FAR 1 FRR 1/3 | t=0.3: FAR 2/3 FRR 1/3 | t=0.8: FAR 1/3 FRR 1/3 -> exact 1/3 @ 0.8
e2i, t2i = eer([0.9, 0.85, 0.2], [0.8, 0.3, 0.25])
check("T2i eer -> 1/3 at 0.8", close(e2i, 1 / 3) and close(t2i, 0.8), f"got {e2i},{t2i}")
# a real interpolation case: pos [0.9, 0.8, 0.7, 0.6, 0.2], neg [0.65, 0.3, 0.1]
#   t=0.2: FAR 1 FRR 0 | t=0.3: FAR 2/3 FRR 1/5 | t=0.6: FAR 1/3 FRR 1/5 | t=0.65: FAR 1/3 FRR 2/5
#   FAR>FRR at 0.6 (d=+2/15), FAR<FRR at 0.65 (d=-1/15): alpha = (2/15)/(3/15) = 2/3
#   thr = 0.6 + 2/3*0.05 = 0.63333..., EER = 1/3 + 2/3*(1/3-1/3) = 1/3
e2j, t2j = eer([0.9, 0.8, 0.7, 0.6, 0.2], [0.65, 0.3, 0.1])
check("T2j interpolated eer -> 1/3 at 0.6333", close(e2j, 1 / 3, 1e-9) and close(t2j, 0.6 + 0.05 * 2 / 3, 1e-9), f"got {e2j},{t2j}")

# T3 — decide at, above and below the threshold
check("T3 decide at threshold -> owner", decide(0.5, 0.5) is True)
check("T3 decide above -> owner", decide(0.51, 0.5) is True)
check("T3 decide below -> not owner", decide(0.49, 0.5) is False)
check("T3 score of identical vectors == 1", close(score([1.0, 2.0, 3.0], [2.0, 4.0, 6.0]), 1.0))
check("T3 score of orthogonal vectors == 0", close(score([1.0, 0.0], [0.0, 5.0]), 0.0))

# T4 — the short-clip refusal rule
check("T4 1.99 s refused", refused_for_length(1.99) is True)
check("T4 2.0 s accepted", refused_for_length(2.0) is False)
check("T4 MIN_CLIP_S is 2.0", MIN_CLIP_S == 2.0)

# centroid helper used by enrollment
c = build_centroid([[3.0, 0.0], [0.0, 4.0]])
check("centroid of two unit-normalised axes is the diagonal", close(c[0], c[1]) and close(c[0] ** 2 + c[1] ** 2, 1.0), f"got {c}")

# T5 — EnrollmentStore JSON round-trip through JARVIS_VOICE_HOME in a temp dir (vectors as plain lists)
with tempfile.TemporaryDirectory() as td:
    os.environ["JARVIS_VOICE_HOME"] = td
    from jarvis_voice.enroll import EnrollmentStore  # noqa: E402  (reads the env at call time)
    from jarvis_voice.paths import voice_home  # noqa: E402
    check("T5 voice_home follows JARVIS_VOICE_HOME", Path(voice_home()) == Path(td))
    st = EnrollmentStore(name="owner")
    p = st.save([0.6, 0.8], [{"path": "a.wav", "sha256": "00", "duration_s": 25.0}], [[0.6, 0.8], [0.8, 0.6]],
                threshold=0.41, model="test-model")
    check("T5 owner.json lives under <home>/enroll", p == Path(td) / "enroll" / "owner.json" and p.exists())
    back = st.load()
    check("T5 round-trip centroid", back["centroid"] == [0.6, 0.8])
    check("T5 round-trip threshold/model/dim", back["threshold"] == 0.41 and back["model"] == "test-model" and back["dim"] == 2)
    check("T5 round-trip clips + vectors", back["clips"][0]["duration_s"] == 25.0 and back["clip_vectors"] == [[0.6, 0.8], [0.8, 0.6]])
    del os.environ["JARVIS_VOICE_HOME"]

# T6 — the deletion rule: delete only after a successful write; keep keeps; a failing write leaves the input
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    inp = td / "in.wav"; inp.write_bytes(b"RIFF....")
    out = td / "t.json"
    r = finalize(inp, out, {"text": "hi"}, keep=False)
    check("T6 keep=False removes the input", not inp.exists())
    check("T6 keep=False JSON says deleted: true", r["deleted"] is True and out.exists())
    inp.write_bytes(b"RIFF....")
    r = finalize(inp, td / "t2.json", {"text": "hi"}, keep=True)
    check("T6 keep=True keeps the input", inp.exists() and r["deleted"] is False and r["kept_by_request"] is True)

    def failing_writer(path, payload):
        raise OSError("disk full (simulated)")
    raised = False
    try:
        finalize(inp, td / "t3.json", {"text": "hi"}, keep=False, writer=failing_writer)
    except OSError:
        raised = True
    check("T6 a failing write raises and leaves the input in place", raised and inp.exists() and not (td / "t3.json").exists())
    # a writer that succeeds must have fsync'd a complete JSON before deletion happened
    import json as _json
    with open(out, encoding="utf-8") as fh:
        check("T6 the written JSON is complete and carries deleted: true", _json.load(fh)["deleted"] is True)

# T7 — speech_mask / runs (the split's pure logic); T = True, F = False
from jarvis_voice.split import speech_mask, runs, pack_runs  # noqa: E402
from jarvis_voice.evaluate import paths_from_json  # noqa: E402
T, F = True, False
fdb = [-60, -60, -30, -30, -60, -60, -60, -30, -60, -60]
check("T7a mask pad 0 gap 0", speech_mask(fdb, -45.0, 0, 0) == [F, F, T, T, F, F, F, T, F, F], str(speech_mask(fdb, -45.0, 0, 0)))
check("T7b mask pad 1 gap 0", speech_mask(fdb, -45.0, 1, 0) == [F, T, T, T, T, F, T, T, T, F], str(speech_mask(fdb, -45.0, 1, 0)))
check("T7c mask pad 0 gap 4 merges the 3-frame gap", speech_mask(fdb, -45.0, 0, 4) == [F, F, T, T, T, T, T, T, F, F], str(speech_mask(fdb, -45.0, 0, 4)))
check("T7d mask pad 0 gap 3 keeps the 3-frame gap (3 is not < 3)", speech_mask(fdb, -45.0, 0, 3) == speech_mask(fdb, -45.0, 0, 0))
check("T7e all-quiet -> all False", speech_mask([-60] * 5, -45.0, 2, 2) == [F] * 5)
check("T7f all-loud -> all True", speech_mask([-30] * 3, -45.0, 2, 2) == [T] * 3)
check("T7 runs", runs([F, T, T, F, T, F, F, T]) == [(1, 3), (4, 5), (7, 8)], str(runs([F, T, T, F, T, F, F, T])))
check("T7 runs empty", runs([]) == [])
check("T7 runs single", runs([T]) == [(0, 1)])

# T8 — pack_runs: close the piece the moment its total >= target; keep the tail iff >= min_keep
check("T8a [4]*5, 10, 3 -> [[0,1,2],[3,4]]", pack_runs([4, 4, 4, 4, 4], 10, 3) == [[0, 1, 2], [3, 4]], str(pack_runs([4, 4, 4, 4, 4], 10, 3)))
check("T8b [4]*5, 10, 9 -> [[0,1,2]] (8 s tail < 9 dropped)", pack_runs([4, 4, 4, 4, 4], 10, 9) == [[0, 1, 2]])
check("T8c [12], 10, 3 -> [[0]]", pack_runs([12], 10, 3) == [[0]])
check("T8d [1,1], 10, 3 -> []", pack_runs([1, 1], 10, 3) == [])
check("T8e [], 10, 3 -> []", pack_runs([], 10, 3) == [])
check("T8f [5,5,2], 10, 3 -> [[0,1]] (2 s tail dropped)", pack_runs([5, 5, 2], 10, 3) == [[0, 1]])

# T9 — paths_from_json
import json as _json2  # noqa: E402
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    (td / "a.json").write_text(_json2.dumps({"sets": {"negatives": ["a.flac", "b.wav"]}}), encoding="utf-8")
    (td / "b.json").write_text(_json2.dumps(["a.flac", "b.wav"]), encoding="utf-8")
    (td / "c.json").write_text(_json2.dumps({"other": 1}), encoding="utf-8")
    check("T9 sets.negatives -> 2 paths", len(paths_from_json(td / "a.json")) == 2)
    check("T9 top-level list -> 2 paths", len(paths_from_json(td / "b.json")) == 2)
    check("T9 other -> [] without exception", paths_from_json(td / "c.json") == [])

# ================================================== T10 — M1a: the clustering decision surface
# NOTE ON LABELS: the prompt for this milestone dictates T7a-T7d for these checks, but T7a-T7f are
# already taken by the split tests above and T8a-T8f by pack_runs. Duplicated labels would make a
# failure ambiguous ("FAIL T7c" matching two different checks), so this block is T10a-T10e and the
# pipeline block is T11a-T11e; the mapping to the prompt's labels is recorded in the report.
import math as _math  # noqa: E402
from collections import Counter as _Counter  # noqa: E402

from jarvis_voice.cluster import (  # noqa: E402
    TAU_GRID, agglomerative_average, assign, choose_tau, completeness, cosine_distance,
    linkage_backend, purity, update_centroid,
)


def _purity_expected(t, p):
    """The definition written a SECOND way, so the check is not the implementation compared with
    itself: group by predicted label, take each group's most common count."""
    g = {}
    for a_, b_ in zip(t, p):
        g.setdefault(b_, []).append(a_)
    return sum(_Counter(v).most_common(1)[0][1] for v in g.values()) / len(t)


def _completeness_expected(t, p):
    g = {}
    for a_, b_ in zip(t, p):
        g.setdefault(a_, []).append(b_)
    return sum(_Counter(v).most_common(1)[0][1] for v in g.values()) / len(t)


_T = ["A"] * 4 + ["B"] * 4 + ["C"] * 4
_P_MISPLACED = [1, 1, 1, 2] + [2, 2, 2, 2] + [3, 3, 3, 3]   # one A lands in B's cluster
_P_ONE = [1] * 12
_P_ALONE = list(range(12))
check("T10a purity and completeness match the definition computed independently, on three labellings",
      close(purity(_T, _P_MISPLACED), _purity_expected(_T, _P_MISPLACED), 1e-12)
      and close(completeness(_T, _P_MISPLACED), _completeness_expected(_T, _P_MISPLACED), 1e-12)
      and close(purity(_T, _P_ONE), _purity_expected(_T, _P_ONE), 1e-12)
      and close(completeness(_T, _P_ONE), _completeness_expected(_T, _P_ONE), 1e-12)
      and close(purity(_T, _P_ALONE), _purity_expected(_T, _P_ALONE), 1e-12)
      and close(completeness(_T, _P_ALONE), _completeness_expected(_T, _P_ALONE), 1e-12)
      # The three anchors the prompt names, and the two failure modes the PRODUCT exists to catch:
      # one cluster maximises completeness, every-item-alone maximises purity.
      and close(purity(_T, _P_MISPLACED), 11 / 12, 1e-12)
      and close(completeness(_T, _P_MISPLACED), 11 / 12, 1e-12)
      and close(purity(_T, _P_ONE), 4 / 12, 1e-12)
      and close(completeness(_T, _P_ONE), 1.0, 1e-12)
      and close(purity(_T, _P_ALONE), 1.0, 1e-12)
      # 3/12, not 4/12: three speakers, each speaker's largest cluster holding exactly one item.
      # The prompt's illustrative 4/12 for this case is an arithmetic slip; the definition in its
      # own section 0 gives 3/12, and the other two cases match it exactly.
      and close(completeness(_T, _P_ALONE), 3 / 12, 1e-12),
      str((purity(_T, _P_MISPLACED), completeness(_T, _P_MISPLACED),
           purity(_T, _P_ONE), completeness(_T, _P_ONE),
           purity(_T, _P_ALONE), completeness(_T, _P_ALONE))))

check("T10b choose_tau takes the max purity x completeness, ties to the SMALLER tau",
      choose_tau([(0.20, 0.9, 0.5), (0.30, 0.95, 0.92), (0.40, 0.8, 0.8)]) == 0.30
      and choose_tau([(0.24, 0.9, 0.8), (0.36, 0.8, 0.9)]) == 0.24
      and choose_tau([(0.50, 0.7, 0.7), (0.22, 0.7, 0.7)]) == 0.22
      and len(TAU_GRID) == 21 and TAU_GRID[0] == 0.20 and TAU_GRID[-1] == 0.60,
      str((choose_tau([(0.24, 0.9, 0.8), (0.36, 0.8, 0.9)]), TAU_GRID[:3], TAU_GRID[-1])))

_OWN = [1.0, 0.0, 0.0, 0.0]
_NEAR_OWNER = [0.6, 0.8, 0.0, 0.0]           # cos 0.6 against the owner centroid, exactly
_C1 = {"centroid": [0.0, 1.0, 0.0, 0.0], "n": 3}
_C2 = {"centroid": [0.0, 0.0, 1.0, 0.0], "n": 2}
_FAR = [0.0, 0.0, 0.0, 1.0]
_TIE = [0.0, 0.7071067811865476, 0.7071067811865476, 0.0]   # equidistant from _C1 and _C2
check("T10c assign: the owner is checked FIRST and his boundary is inclusive; then nearest within "
      "tau; then a new cluster; ties to the lowest index",
      # exactly AT the threshold -> owner, even though the sole cluster is a perfect distance-0
      # match for the same vector. This is the ordering M1 depends on.
      assign(_NEAR_OWNER, _OWN, 0.6, [{"centroid": _NEAR_OWNER, "n": 1}], 0.5) == (-1, "owner")
      and assign(_C1["centroid"], _OWN, 0.9, [_C1, _C2], 0.5) == (0, "join")
      and assign(_C2["centroid"], _OWN, 0.9, [_C1, _C2], 0.5) == (1, "join")
      and assign(_FAR, _OWN, 0.9, [_C1, _C2], 0.5) == (-1, "new")
      and assign(_TIE, _OWN, 0.9, [_C1, _C2], 0.5) == (0, "join")
      and assign(_FAR, _OWN, 0.9, [], 0.5) == (-1, "new"),
      str((assign(_NEAR_OWNER, _OWN, 0.6, [{"centroid": _NEAR_OWNER, "n": 1}], 0.5),
           assign(_TIE, _OWN, 0.9, [_C1, _C2], 0.5))))

_V1, _V2, _V3 = [1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]


def _step_expected(c, n, e):
    """One step of the definition, written independently: the mean over n+1, then normalised."""
    m = [(ci * n + ei) / (n + 1) for ci, ei in zip(c, e)]
    q = _math.sqrt(sum(x * x for x in m))
    return [x / q for x in m]


_after2 = update_centroid(_V1, 1, _V2)
_after3 = update_centroid(_after2, 2, _V3)
_exp2 = _step_expected(_V1, 1, _V2)
_exp3 = _step_expected(_after2, 2, _V3)
# NOTE the accumulated result is NOT the normalised mean of the three RAW vectors: re-normalising
# at every step discards each intermediate magnitude, so the running centroid weights recent spans
# slightly more. That is the dictated behaviour ("the running mean ... re-normalised"), and it is
# checked step by step against the definition rather than against the raw mean, which it does not
# equal ([0.632, 0.632, 0.447] here, against the raw mean's [0.577, 0.577, 0.577]).
check("T10d update_centroid is the normalised running mean over n+1, step by step",
      all(close(a, b, 1e-12) for a, b in zip(_after2, _exp2))
      and all(close(a, b, 1e-12) for a, b in zip(_after3, _exp3))
      and close(sum(x * x for x in _after2), 1.0, 1e-12)
      and close(sum(x * x for x in _after3), 1.0, 1e-12)
      and close(cosine_distance(_V1, _V1), 0.0, 1e-12)
      and close(cosine_distance(_V1, _V2), 1.0, 1e-12),
      str((_after3, _exp3)))

# `agglomerative_average` is the ONE function here that needs numpy or scipy, and this suite runs
# stdlib-only in CI. The backend NAME is checked unconditionally (it is pure); the clustering itself
# runs only where a backend exists, and the skip is ANNOUNCED in the check name rather than passing
# quietly - a check that silently evaporates on the runner is worse than no check.
try:
    import numpy as _np_probe  # noqa: F401
    _HAVE_LINKAGE = True
except Exception:
    _HAVE_LINKAGE = False

check("T10e linkage_backend names one of the two implementations (pure, always checked)",
      linkage_backend() in ("scipy.cluster.hierarchy", "numpy-average-linkage"),
      linkage_backend())
if _HAVE_LINKAGE:
    _LAB = agglomerative_average([[1.0, 0.0, 0.0], [0.99, 0.14, 0.0],
                                  [0.0, 0.0, 1.0], [0.0, 0.14, 0.99]], 0.30)
    check("T10f agglomerative_average merges within tau and separates beyond it",
          len(set(_LAB)) == 2 and _LAB[0] == _LAB[1] and _LAB[2] == _LAB[3] and _LAB[0] != _LAB[2],
          str((_LAB, linkage_backend())))
else:
    check("T10f agglomerative_average SKIPPED - no numpy/scipy on this runner (stdlib-only)",
          True, "backend would be %s" % linkage_backend())


import jarvis_voice.spine as _spine_mod  # noqa: E402,F401  (inserts phase7/memory on sys.path)
from jarvis_memory.store import MemoryStore  # noqa: E402

# ================================================== T11 — M1b: the pipeline's rules
# Labels: the prompt dictates T8a-T8e; T8a-T8f are already taken by pack_runs above, so this block
# is T11 (the mapping is in the report). No GPU, no model, no corpus: the ASR and the embedder are
# fakes, which is the only way to exercise the DELETION ORDER, the one rule whose failure is
# irreversible.
import wave as _wave  # noqa: E402

from jarvis_voice.cluster import fill_adjacent  # noqa: E402
from jarvis_voice.spine import SPAN_KEYS, ingest_one, longest_span_texts  # noqa: E402
from jarvis_voice.transcribe import resolve_started_at  # noqa: E402


class _FakeASR:
    def __init__(self, segs, duration=30.0):
        self.segs, self.duration = segs, duration

    def run(self, path, options=None):
        self.options = options
        return {"model": "fake", "duration_s": self.duration, "wall_s": 0.1, "rtf": 0.003,
                "language": "en", "segments": list(self.segs),
                "text": "".join(s["text"] for s in self.segs)}


def _fake_load_wav(path):
    """A stub reader: the fake embedder ignores the samples, so the suite needs no audio decoder."""
    return [0.0] * 16000, 16000


def _fake_load_wav_long(path):
    """The same stub, 40 seconds long.

    Length is LOAD-BEARING wherever a test asserts that a turn was NOT embedded: with a one-second
    stub every segment past the first second slices to nothing and is skipped for want of samples,
    so an assertion meant to pin the minimum-duration rule would pass with the rule deleted. Found
    by the mutant that removed the rule and did not bite.
    """
    return [0.0] * (16000 * 40), 16000


class _FakeEmbedder:
    """Returns a unit vector in one of two far-apart directions, chosen by the segment's text."""
    def __init__(self, owner_dir=0):
        self.owner_dir = owner_dir
        self.calls = 0

    def embed(self, wav, sr):
        self.calls += 1
        v = [0.0] * 192
        v[0 if self.owner_dir == 0 else 1] = 1.0
        return v


def _wav(path, seconds=6.0, sr=16000):
    with _wave.open(str(path), "wb") as fh:
        fh.setnchannels(1)
        fh.setsampwidth(2)
        fh.setframerate(sr)
        fh.writeframes(b"\x00\x00" * int(seconds * sr))
    return path


def _seg(a, b, text="hello there this is a span"):
    return {"start": a, "end": b, "text": text, "avg_logprob": -0.25, "no_speech_prob": 0.01}


_ENR = {"centroid": [1.0] + [0.0] * 191, "threshold": 0.5}

with tempfile.TemporaryDirectory() as _td11:
    _td11 = Path(_td11)
    os.environ["JARVIS_VOICE_HOME"] = str(_td11 / "voicehome")

    # ---- T11a started_at precedence
    _p_stamp = _td11 / "rec_20260910_143000.wav"
    _p_plain = _td11 / "plain.wav"
    check("T11a started_at: argument beats the filename stamp beats mtime-duration, and the SOURCE "
          "is recorded",
          resolve_started_at(_p_stamp, argument="2026-01-02T03:04:05+10:00")
          == ("2026-01-02T03:04:05+10:00", "argument")
          and resolve_started_at(_p_stamp)[1] == "filename"
          and resolve_started_at(_p_stamp)[0].startswith("2026-09-10T14:30:00")
          and resolve_started_at(_p_plain, duration_s=60, mtime=1_800_000_000)[1] == "mtime",
          str((resolve_started_at(_p_stamp), resolve_started_at(_p_plain, duration_s=60,
                                                                mtime=1_800_000_000))))

    # ---- T11b adjacency for spans under 2 s
    check("T11b a short span takes the PREVIOUS span's cluster, the next one's when it is first, "
          "and stays unassigned when the recording embedded nothing",
          fill_adjacent([2, None, 3]) == [2, 2, 3]          # previous (2), not nearer-in-time (3)
          and fill_adjacent([None, 2, None, 3, None]) == [2, 2, 2, 3, 3]
          and fill_adjacent([None, None, None]) == [None, None, None]
          and fill_adjacent([]) == [],
          str((fill_adjacent([2, None, 3]), fill_adjacent([None, 2, None, 3, None]))))

    # ---- T11c the deletion order — the irreversible rule
    # the third segment deliberately ends BEYOND the 30 s recording: Whisper does this on
    # real audio (measured 47.98 s on a 20.0 s file) and an unclamped end enters the spine.
    _asr = _FakeASR([_seg(0.0, 5.0), _seg(5.0, 5.5, "yeah"), _seg(5.5, 44.0)], duration=30.0)

    class _CommitRaises:
        """A store that fails at the very last step of the transaction."""
        def __init__(self, real):
            self._real = real
            self.path = real.path
            self.conn = real.conn

        def __getattr__(self, k):
            return getattr(self._real, k)

        def promote_persons(self):
            raise RuntimeError("commit failed")

    _w1 = _wav(_td11 / "a.wav")
    _store1 = MemoryStore(":memory:")
    _raised = False
    try:
        ingest_one(_w1, _CommitRaises(_store1), _ENR, 0.5, _asr, _FakeEmbedder(),
                   out_dir=_td11 / "t1", load_wav=_fake_load_wav, min_embed_s=2.0)
    except Exception:
        _raised = True
    _spans_after_fail = _store1.conn.execute("select count(*) from span").fetchone()[0]
    _recs_after_fail = _store1.conn.execute("select count(*) from recording").fetchone()[0]

    def _writer_raises(path, payload):
        raise IOError("disk full")

    _w2 = _wav(_td11 / "b.wav")
    _store2 = MemoryStore(":memory:")
    _raised2 = False
    try:
        ingest_one(_w2, _store2, _ENR, 0.5, _asr, _FakeEmbedder(), out_dir=_td11 / "t2",
                   writer=_writer_raises, load_wav=_fake_load_wav, min_embed_s=2.0)
    except Exception:
        _raised2 = True

    _w3 = _wav(_td11 / "c.wav")
    _store3 = MemoryStore(":memory:")
    # Guarded so a regression in the ORDER fails BY NAME rather than crashing the suite: a build
    # that deletes the audio before `finalize` runs raises FileNotFoundError here, and a traceback
    # would say far less than a named failing check does.
    try:
        _ok3 = ingest_one(_w3, _store3, _ENR, 0.5, _asr, _FakeEmbedder(), out_dir=_td11 / "t3",
                          load_wav=_fake_load_wav, min_embed_s=2.0)
    except Exception as _exc3:
        _ok3 = {"deleted": "RAISED: %s" % _exc3, "store_committed": False, "spans": [],
                "clusters_after": {}, "started_at_source": None, "tau": None,
                "recording_id": None, "embedded_spans": -1}
    check("T11c the audio survives every failure and is deleted ONLY after the store committed and "
          "the JSON was written",
          _raised and _w1.exists() and _spans_after_fail == 0 and _recs_after_fail == 0
          and _raised2 and _w2.exists()
          and _ok3["deleted"] is True and not _w3.exists()
          and _ok3["store_committed"] is True
          and (_td11 / "t3" / "c.json").exists(),
          str((_raised, _w1.exists(), _spans_after_fail, _raised2, _w2.exists(),
               _ok3["deleted"], _w3.exists())))

    # ---- T11d the transcript JSON's shape
    _spans3 = _ok3["spans"]
    _counts3 = {}
    for _s in _spans3:
        if _s["cluster_id"] is not None:
            _counts3[str(_s["cluster_id"])] = _counts3.get(str(_s["cluster_id"]), 0) + 1
    # Since M1b.3 these three segments are ONE turn — they are back to back, so no gap opens a new
    # one — and the turn is what carries an embedding. Before M1b.3 the 0.5 s span was skipped and
    # took its neighbour's cluster; now it is inside the turn that was scored, which is the point.
    _turns3 = _ok3["turns"]
    check("T11d every span carries the nine keys, belongs to a turn, and clusters_after matches the "
          "assignments",
          all(set(s) == set(SPAN_KEYS) for s in _spans3)
          and len(_spans3) == 3
          and _ok3["clusters_after"] == _counts3
          and _ok3["started_at_source"] in ("argument", "filename", "mtime")
          and _ok3["tau"] == 0.5 and _ok3["recording_id"] is not None
          and _ok3["min_embed_s"] == 2.0
          # one turn over all three segments; ONE embedding, not one per span
          and len(_turns3) == 1 and _turns3[0]["n_segments"] == 3
          and _turns3[0]["speech_s"] == 30.0 and _turns3[0]["embedded"] is True
          and _ok3["embedded_turns"] == 1 and _ok3["embedded_spans"] == 1
          and all(s["turn_id"] == 1 for s in _spans3)
          and {s["cluster_source"] for s in _spans3} == {_turns3[0]["cluster_source"]}
          # the over-long segment is clamped to the recording, never stored beyond it
          and _spans3[2]["t_end_s"] == 30.0 and all(s["t_end_s"] <= 30.0 for s in _spans3),
          str((sorted(_spans3[0]), _ok3["clusters_after"], _counts3, _turns3,
               [s["cluster_source"] for s in _spans3])))

    # ---- T11e the listing truncates and never writes
    _long = "x" * 500
    _r5 = _store3.add_recording("sha-5", "2026-03-01T08:00:00", 60.0, "headset")
    _c5 = _store3.add_cluster()
    _store3.add_span(_r5, 0.0, 40.0, _c5, _long, -0.2)
    _before = sorted(p.name for p in (_td11 / "t3").iterdir())
    _texts = longest_span_texts(_store3, _c5)
    _after = sorted(p.name for p in (_td11 / "t3").iterdir())
    check("T11e the cluster listing truncates span text to 60 characters and writes no file",
          _texts and all(len(t) <= 60 for t in _texts) and _texts[0] == "x" * 60
          and _before == _after,
          str(([len(t) for t in _texts], _before == _after)))

    os.environ.pop("JARVIS_VOICE_HOME", None)


# ================================================== T12 — M1a.2: the duration measurement's rules
# The threshold is a property of a voice AT A DURATION: M0b measured it on ~10 s pieces and the M1
# pipeline applied it to 1-3 s spans, which is why the owner failed his own check. These two rules
# decide what the measurement means, so both are pinned here with no GPU and no corpus.
from jarvis_voice.duration import (  # noqa: E402
    DURATION_GRID, MAX_FAR, MAX_FRR, MIN_N, choose_min_embed_s, latest_duration_bench,
    reread_duration_bench, row_has_sample_floor, speech_windows, speech_windows_stream,
)

# three runs of 2, 3 and 4 s at 0.05 s per frame -> 40, 60 and 80 frames
_RUNS = [(0, 40), (40, 100), (100, 180)]
check("T12a speech_windows packs runs in order to at least D and DROPS a short tail",
      speech_windows(_RUNS, 0.05, 5.0) == [[0, 1]]                 # 2+3 = 5 s; the 4 s tail dropped
      and speech_windows(_RUNS, 0.05, 2.0) == [[0], [1], [2]]      # each run reaches 2 s alone
      and speech_windows(_RUNS, 0.05, 12.0) == []                  # 9 s total never reaches 12
      and speech_windows([], 0.05, 3.0) == []
      and speech_windows(_RUNS, 0.05, 9.0) == [[0, 1, 2]],         # exactly the whole file
      str((speech_windows(_RUNS, 0.05, 5.0), speech_windows(_RUNS, 0.05, 2.0),
           speech_windows(_RUNS, 0.05, 12.0))))


def _row(d, frr, far):
    """A table row for the BAND half of the rule.

    `n_pos`/`n_neg` are set AT the sample floor (M1a.4) so every row here is eligible and the check
    stays about the bands alone; the floor's own behaviour is T14a's.
    """
    return {"d_s": d, "n_pos": MIN_N, "n_neg": MIN_N,
            "frr_at_stored": frr, "far_at_stored": far}


# Hand-built tables. The qualifying rows are computed here from the same bands the module exposes,
# so the expectation is not a typed constant that could drift from the rule it checks.
_TBL = [_row(1.0, 0.40, 0.05), _row(2.0, 0.12, 0.02), _row(3.0, 0.04, 0.005),
        _row(5.0, 0.00, 0.000), _row(8.0, 0.00, 0.000), _row(12.0, 0.00, 0.000)]
_QUALIFY = [r["d_s"] for r in _TBL if r["frr_at_stored"] <= MAX_FRR and r["far_at_stored"] <= MAX_FAR]
_TBL_NONE = [_row(1.0, 0.40, 0.05), _row(2.0, 0.30, 0.03), _row(3.0, 0.20, 0.02)]
# FRR inside the band but FAR outside it must NOT qualify - both bands, not either.
_TBL_FAR_ONLY = [_row(1.0, 0.01, 0.50), _row(2.0, 0.01, 0.02), _row(3.0, 0.01, 0.004)]
check("T12b the reading rule takes the SMALLEST duration meeting BOTH bands, else None",
      choose_min_embed_s(_TBL) == min(_QUALIFY) and choose_min_embed_s(_TBL) == 3.0
      and choose_min_embed_s(_TBL_NONE) is None
      and choose_min_embed_s(_TBL_FAR_ONLY) == 3.0
      and choose_min_embed_s([]) is None
      and (MAX_FRR, MAX_FAR) == (0.05, 0.01)
      and DURATION_GRID == (1.0, 2.0, 3.0, 5.0, 8.0, 12.0),
      str((choose_min_embed_s(_TBL), _QUALIFY, choose_min_embed_s(_TBL_FAR_ONLY))))


# ============================================ T12c/T12d — M1a.3: the bench's own rule is a STREAM cut
# T12a's atom rule stays where it belongs (packing enrollment pieces). The bench needed a different
# one: every one of the owner's held-out pieces is ONE continuous run, so the atom rule made the
# table flat by construction and the reading rule returned the grid's floor from thirteen identical
# measurements. The expectations below are DERIVED from _RUNS rather than typed, so a change to the
# fixture cannot leave a stale constant passing.
_FS = 0.05
_LENS = [b - a for a, b in _RUNS]                     # 40, 60, 80 frames = 2, 3, 4 s
_TOTAL = sum(_LENS)                                   # 180 frames = 9 s of speech
_BASE = _RUNS[0][0]


def _spans(wins):
    """Each window as (first frame, last frame, frame total) across its slices."""
    return [(w[0][1], w[-1][2], sum(e - a for (_i, a, e) in w)) for w in wins]


def _expected_spans(d_s):
    """The windows _RUNS must yield at D, computed from the run lengths.

    These runs are contiguous in the frame timeline, so the concatenated speech stream and the
    absolute frame index coincide and the expectation is one piece of arithmetic: floor(total/need)
    windows of exactly `need` frames each, laid end to end from the first run's start.
    """
    need = int(round(d_s / _FS))
    n = _TOTAL // need if need > 0 else 0
    return [(_BASE + k * need, _BASE + (k + 1) * need, need) for k in range(n)]


_w2 = speech_windows_stream(_RUNS, _FS, 2.0)
_need2 = int(round(2.0 / _FS))
# Which window straddles the run-1/run-2 boundary is arithmetic too, not an eyeballed index.
_boundary = _RUNS[1][1] - _BASE                       # 100 frames of speech before run 2 starts
_straddle_k = _boundary // _need2                     # == 2 here, because 100 % 40 != 0
check("T12c the stream cut takes exactly D seconds of speech, splitting runs and dropping the tail",
      _spans(_w2) == _expected_spans(2.0) and len(_w2) == _TOTAL // _need2 == 4
      and _boundary % _need2 != 0
      and sorted({i for (i, _a, _e) in _w2[_straddle_k]}) == [1, 2]
      and all(len({i for (i, _a, _e) in w}) == 1 for k, w in enumerate(_w2) if k != _straddle_k)
      and _spans(speech_windows_stream(_RUNS, _FS, 5.0)) == _expected_spans(5.0)
      and len(speech_windows_stream(_RUNS, _FS, 5.0)) == 1
      and _spans(speech_windows_stream(_RUNS, _FS, 9.0)) == _expected_spans(9.0)
      and len(speech_windows_stream(_RUNS, _FS, 9.0)) == 1
      and speech_windows_stream(_RUNS, _FS, 12.0) == [] == _expected_spans(12.0)
      and speech_windows_stream([], _FS, 3.0) == []
      # and the atom rule is untouched by any of it
      and speech_windows(_RUNS, _FS, 5.0) == [[0, 1]],
      str((_spans(_w2), _expected_spans(2.0), _straddle_k)))

# T12d — the pipeline reads a minimum only from a bench that declares the stream rule, and a
# superseded run parked under a suffixed name is invisible to the date-shaped glob.
_td12 = Path(tempfile.mkdtemp(prefix="jv_t12d_"))
import json as _json12  # noqa: E402

_empty_ok = False
try:
    latest_duration_bench(_td12)
except SystemExit as _e:
    _empty_ok = "duration_bench" in str(_e)

(_td12 / "duration_bench_2026-01-01.json").write_text(
    _json12.dumps({"min_embed_s": 1.0}), encoding="utf-8")          # the atom-rule shape: no rule
_refused = False
try:
    latest_duration_bench(_td12)
except SystemExit as _e:
    _refused = "window_rule" in str(_e)

(_td12 / "duration_bench_2026-01-02.json").write_text(
    _json12.dumps({"window_rule": "stream", "min_embed_s": 3.0}), encoding="utf-8")
_accepted = latest_duration_bench(_td12)

# A suffixed name sorts AFTER every date-named file; it must not be the one that is read.
(_td12 / "duration_bench_2026-01-03_atoms.json").write_text(
    _json12.dumps({"window_rule": "stream", "min_embed_s": 99.0}), encoding="utf-8")
_still = latest_duration_bench(_td12)

check("T12d the minimum is read only from a bench declaring the stream rule; a suffixed file is not read",
      _empty_ok and _refused and _accepted["min_embed_s"] == 3.0
      and _still["min_embed_s"] == 3.0,
      str((_empty_ok, _refused, _accepted.get("min_embed_s"), _still.get("min_embed_s"))))

# ================================ T13a/T13b — M1b.2: deterministic ASR and the double-run guard
# M1b transcribed a byte-identical input twice under identical settings and got 3 segments once and
# 9 the other time. Everything downstream follows the segmentation and the audio is deleted at the
# end, so an unreproducible segmentation makes the spine a one-shot record. Two answers: pin the
# decoder, and check on every run whether the pin held while the audio still exists.
from jarvis_voice.transcribe import (  # noqa: E402
    ASR, ASR_GUARD_TOL_S, M0A_ASR_OPTIONS, PIPELINE_ASR_OPTIONS, segmentations_agree,
)


class _CapturingInfo:
    language, language_probability, duration = "en", 0.99, 10.0


class _CapturingModel:
    """Stands in for faster-whisper's WhisperModel: records the kwargs, returns no segments."""
    def __init__(self):
        self.kwargs = None

    def transcribe(self, audio, **kw):
        self.kwargs = dict(kw)
        return iter(()), _CapturingInfo()


# ASR.__init__ would import faster-whisper and load a 3 GB model, so the object is built without it
# — the method under test is `run`, and what it must be pinned on is the kwargs it forwards.
_asr13 = ASR.__new__(ASR)
_asr13.model_name, _asr13.compute_type, _asr13.device = "fake", "float16", "cuda"
_asr13.version, _asr13.vad_parameters = "1.2.1", {"threshold": 0.5}
_asr13.model = _CapturingModel()

_r_pipe = _asr13.run("x.wav", PIPELINE_ASR_OPTIONS)
_kw_pipe = _asr13.model.kwargs
_r_m0a = _asr13.run("x.wav")
_kw_m0a = _asr13.model.kwargs
check("T13a the pipeline decodes with the pinned deterministic settings and records them; the M0a "
      "command's settings are unchanged",
      _kw_pipe == PIPELINE_ASR_OPTIONS
      and PIPELINE_ASR_OPTIONS == {"temperature": 0.0, "beam_size": 5, "vad_filter": True,
                                   "condition_on_previous_text": False}
      and _kw_m0a == M0A_ASR_OPTIONS == {"beam_size": 5, "vad_filter": False}
      and _r_pipe["asr_options"] == PIPELINE_ASR_OPTIONS
      and _r_pipe["vad_parameters"] == {"threshold": 0.5}
      and _r_m0a["asr_options"] == M0A_ASR_OPTIONS
      # the caller's dict is never mutated by the run, so a second run cannot inherit a change
      and PIPELINE_ASR_OPTIONS is not _r_pipe["asr_options"],
      str((_kw_pipe, _kw_m0a)))


class _TwoPassASR:
    """A different segmentation per call, so the guard's disagreement branch is reachable."""
    def __init__(self, first, second, duration=30.0):
        self.passes, self.duration, self.calls, self.options = [first, second], duration, 0, []

    def run(self, path, options=None):
        self.options.append(options)
        segs = self.passes[min(self.calls, len(self.passes) - 1)]
        self.calls += 1
        return {"model": "fake", "duration_s": self.duration, "wall_s": 0.1, "rtf": 0.003,
                "language": "en", "segments": list(segs),
                "text": "".join(x["text"] for x in segs)}


_P1 = [_seg(0.0, 5.0), _seg(5.0, 10.0, "second span here")]
_P_SHIFTED = [_seg(0.0, 5.0), _seg(5.0 + 5 * ASR_GUARD_TOL_S, 10.0, "second span here")]
_P_NUDGED = [_seg(0.0, 5.0), _seg(5.0 + ASR_GUARD_TOL_S / 2, 10.0, "second span here")]
_P_RETEXT = [_seg(0.0, 5.0), _seg(5.0, 10.0, "a different second span")]
_P_SPLIT = [_seg(0.0, 5.0)]

with tempfile.TemporaryDirectory() as _td13:
    _td13 = Path(_td13)

    _w_bad = _wav(_td13 / "mismatch.wav")
    _store_bad = MemoryStore(":memory:")
    _asr_bad = _TwoPassASR(_P1, _P_SHIFTED)
    _r_bad = ingest_one(_w_bad, _store_bad, _ENR, 0.5, _asr_bad, _FakeEmbedder(),
                        out_dir=_td13 / "bad", load_wav=_fake_load_wav, min_embed_s=2.0)
    _spans_bad = _store_bad.conn.execute("select count(*) from span").fetchone()[0]
    _recs_bad = _store_bad.conn.execute("select count(*) from recording").fetchone()[0]

    _w_ok = _wav(_td13 / "agree.wav")
    _store_ok = MemoryStore(":memory:")
    _r_ok = ingest_one(_w_ok, _store_ok, _ENR, 0.5, _TwoPassASR(_P1, _P1), _FakeEmbedder(),
                       out_dir=_td13 / "ok", load_wav=_fake_load_wav, min_embed_s=2.0)

    _w_keep = _wav(_td13 / "keep.wav")
    _r_keep = ingest_one(_w_keep, MemoryStore(":memory:"), _ENR, 0.5, _TwoPassASR(_P1, _P1),
                         _FakeEmbedder(), out_dir=_td13 / "keep", keep=True,
                         load_wav=_fake_load_wav, min_embed_s=2.0)

    check("T13b two ASR passes that disagree KEEP the audio and write the first pass once; passes "
          "that agree delete it unless the operator asked",
          # the pure predicate, both directions and every clause
          segmentations_agree(_r0 := {"segments": _P1}, {"segments": _P1})[0] is True
          and segmentations_agree(_r0, {"segments": _P_NUDGED})[0] is True      # inside the tolerance
          and segmentations_agree(_r0, {"segments": _P_SHIFTED})[0] is False    # outside it
          and segmentations_agree(_r0, {"segments": _P_RETEXT})[0] is False     # same times, new text
          and segmentations_agree(_r0, {"segments": _P_SPLIT})[0] is False      # a different count
          and segmentations_agree({"segments": []}, {"segments": []})[0] is True
          # the detail is written to the transcript JSON and printed: it must carry no span text
          and not any(w in str(segmentations_agree(_r0, {"segments": _P_RETEXT})[1])
                      for w in ("span", "hello", "different"))
          # the mismatch run: audio kept, and kept for the GUARD's reason, not the operator's
          and _r_bad["kept_by_guard"] is True and _r_bad["kept_by_request"] is False
          and _r_bad["deleted"] is False and _w_bad.exists()
          and _r_bad["asr_guard"]["agreed"] is False
          and _r_bad["asr_guard"]["max_start_delta_s"] > ASR_GUARD_TOL_S
          # written ONCE, from the FIRST pass
          and _spans_bad == len(_P1) and _recs_bad == 1 and _r_bad["store_committed"] is True
          # both passes ran, both under the pinned settings
          and _asr_bad.calls == 2 and _asr_bad.options == [PIPELINE_ASR_OPTIONS] * 2
          # the agreeing run deletes, and --keep still overrides
          and _r_ok["kept_by_guard"] is False and _r_ok["deleted"] is True and not _w_ok.exists()
          and _r_keep["deleted"] is False and _r_keep["kept_by_request"] is True
          and _r_keep["kept_by_guard"] is False and _w_keep.exists(),
          str((_r_bad["kept_by_guard"], _r_bad["deleted"], _w_bad.exists(), _spans_bad, _recs_bad,
               _asr_bad.calls, _r_ok["deleted"], _r_keep["deleted"])))

# ================================ T13c/T13d — M1b.3: the turn is the embedding unit
# M1a.3 measured the owner's false-reject rate against his OWN threshold at 0.74 on one-second
# windows and 0.00 at twelve, so a per-segment embedding asks the threshold a question it cannot
# answer — which is how M1b opened three new clusters for the owner's own voice. A turn is
# consecutive segments with no real silence between them, and only a turn holding at least the
# MEASURED minimum of speech is embedded.
from jarvis_voice.cluster import (  # noqa: E402
    TURN_MAX_GAP_S, TURN_MAX_SPEECH_S, build_turns,
)
from jarvis_voice.duration import required_min_embed_s  # noqa: E402


def _b(*pairs):
    return [(float(a), float(b)) for a, b in pairs]


# every expectation below is derived from the bounds, not typed
_CAP_SEGS = _b(*[(i * 3.1, i * 3.1 + 3.0) for i in range(5)])       # five 3 s segments, 0.1 s apart
_cap_turns = build_turns(_CAP_SEGS)
_cap_first = _cap_turns[0]
_cap_speech = sum(b - a for a, b in (_CAP_SEGS[i] for i in _cap_first))
# the cap closes the turn on the segment that REACHES it: the first n whose total is >= the cap
_cap_expect = next(n for n in range(1, len(_CAP_SEGS) + 1)
                   if sum(b - a for a, b in _CAP_SEGS[:n]) >= TURN_MAX_SPEECH_S)

_EXACT = _b((0.0, 2.0), (2.0 + TURN_MAX_GAP_S, 4.0 + TURN_MAX_GAP_S))          # gap == the limit
_OVER = _b((0.0, 2.0), (2.0 + TURN_MAX_GAP_S + 0.01, 4.0))                     # gap just over it
_LONE = _b((0.0, TURN_MAX_SPEECH_S + 5.0))                                     # one over-long segment

with tempfile.TemporaryDirectory() as _td13c:
    _td13c = Path(_td13c)

    # a 12 s turn (two segments 0.2 s apart) then a 1 s turn after 7.8 s of silence
    _TSEGS = [_seg(0.0, 6.0), _seg(6.2, 12.2, "still the same person"), _seg(20.0, 21.0, "yeah")]
    _w13c = _wav(_td13c / "turns.wav")
    _store13c = MemoryStore(":memory:")
    _r13c = ingest_one(_w13c, _store13c, _ENR, 0.5, _FakeASR(_TSEGS, duration=30.0),
                       _FakeEmbedder(), out_dir=_td13c / "out", load_wav=_fake_load_wav_long,
                       min_embed_s=3.0)
    _t13c, _s13c = _r13c["turns"], _r13c["spans"]
    _emb13c = _store13c.conn.execute("select count(*) from embedding").fetchone()[0]

    check("T13c turns merge across short gaps, close at the cap, never cross a long gap, and only a "
          "turn holding the measured minimum is embedded - its segments inherit the answer",
          # the pure rule
          build_turns(_EXACT) == [[0, 1]]                  # a gap EQUAL to the limit still merges
          and build_turns(_OVER) == [[0], [1]]             # just over it does not
          and len(_cap_first) == _cap_expect == 4 and _cap_speech >= TURN_MAX_SPEECH_S
          and _cap_turns == [[0, 1, 2, 3], [4]]
          # the cap bounds MERGING, not a segment: one long segment is a turn on its own
          and build_turns(_LONE) == [[0]]
          and build_turns([]) == []
          # through the pipeline: two turns, of 12.0 s and 1.0 s
          and [t["n_segments"] for t in _t13c] == [2, 1]
          and [t["speech_s"] for t in _t13c] == [12.0, 1.0]
          and _t13c[0]["embedded"] is True and _t13c[1]["embedded"] is False
          and _r13c["embedded_turns"] == 1 and _emb13c == 1        # ONE vector, not one per span
          and _t13c[1]["score_owner"] is None                      # never scored, never claimed
          # the segments inherit their turn, and the short turn takes the previous turn's cluster
          and [s["turn_id"] for s in _s13c] == [1, 1, 2]
          and [s["cluster_source"] for s in _s13c] == ["owner", "owner", "adjacent"]
          and _t13c[1]["cluster_id"] == _t13c[0]["cluster_id"]
          and _r13c["min_embed_s"] == 3.0,
          str(([t["speech_s"] for t in _t13c], [s["cluster_source"] for s in _s13c],
               _cap_turns, _emb13c)))

    # ---- T13d the minimum has exactly one source, and the pipeline refuses without it
    import json as _json13  # noqa: E402
    _home13 = _td13c / "home"
    _home13.mkdir()
    _r_empty = _r_atoms = _r_null = None
    try:
        required_min_embed_s(_home13)
    except SystemExit as _e:
        _r_empty = "duration_bench" in str(_e)
    (_home13 / "duration_bench_2026-01-01.json").write_text(
        _json13.dumps({"min_embed_s": 1.0}), encoding="utf-8")
    try:
        required_min_embed_s(_home13)
    except SystemExit as _e:
        _r_atoms = "window_rule" in str(_e)
    (_home13 / "duration_bench_2026-01-02.json").write_text(
        _json13.dumps({"window_rule": "stream", "min_embed_s": None}), encoding="utf-8")
    try:
        required_min_embed_s(_home13)
    except SystemExit as _e:
        _r_null = "STOP" in str(_e)
    (_home13 / "duration_bench_2026-01-03.json").write_text(
        _json13.dumps({"window_rule": "stream", "min_embed_s": 12.0}), encoding="utf-8")
    _got13 = required_min_embed_s(_home13)

    _w13d = _wav(_td13c / "nomin.wav")
    _refused13 = False
    try:
        ingest_one(_w13d, MemoryStore(":memory:"), _ENR, 0.5, _FakeASR(_TSEGS, duration=30.0),
                   _FakeEmbedder(), out_dir=_td13c / "nomin", load_wav=_fake_load_wav)
    except ValueError as _e:
        _refused13 = "min_embed_s" in str(_e)

    check("T13d the minimum comes only from a stream-rule bench that produced one, and the pipeline "
          "refuses to run without it",
          _r_empty is True and _r_atoms is True and _r_null is True
          and _got13 == 12.0 and isinstance(_got13, float)
          and _refused13 and _w13d.exists(),          # refused BEFORE anything touched the audio
          str((_r_empty, _r_atoms, _r_null, _got13, _refused13, _w13d.exists())))

# ============================== T14a — M1a.4: the reading rule's sample floor, and the retraction
# Without a floor the rule returned 12.0 s from a row holding TWO positive windows, and it preferred
# that row precisely BECAUSE it was the sparsest: at n_pos = 14 the finest non-zero FRR expressible
# is 1/14 = 0.0714, already outside the 5 % band, so only an exact zero can pass and the fewest
# windows is the likeliest place to find one. Every expectation below is computed from the bands and
# the counts rather than typed, so it cannot drift from the rule it checks.

# M1a.3's measured table, its six rows as data (goal doc §6 / REPORT-VOICE-M1C-PREP-2.md §2).
def _drow(d, n_pos, n_neg, frr, far):
    return {"d_s": d, "n_pos": n_pos, "n_neg": n_neg,
            "frr_at_stored": frr, "far_at_stored": far}


_M1A3 = [_drow(1.0, 143, 702, 0.7413, 0.0), _drow(2.0, 69, 333, 0.4783, 0.0),
         _drow(3.0, 42, 209, 0.3571, 0.0), _drow(5.0, 27, 104, 0.2222, 0.0),
         _drow(8.0, 14, 54, 0.1429, 0.0), _drow(12.0, 2, 22, 0.0, 0.0)]
_bands_ok = [r for r in _M1A3 if r["frr_at_stored"] <= MAX_FRR and r["far_at_stored"] <= MAX_FAR]
_floor_ok = [r for r in _M1A3 if row_has_sample_floor(r)]
_sparse = _drow(0.5, 2, 22, 0.0, 0.0)                    # a zero on nothing
_solid = _drow(4.0, 25, 25, 0.04, 0.0)                   # a rate with a denominator

with tempfile.TemporaryDirectory() as _td14:
    _td14 = Path(_td14)
    import json as _json14  # noqa: E402
    _bench14 = {"window_rule": "stream", "min_embed_s": 12.0, "stored_threshold": 0.358503,
                "table": _M1A3}
    _bp14 = _td14 / "duration_bench_2026-09-10.json"
    _bp14.write_text(_json14.dumps(_bench14, indent=1), encoding="utf-8")
    _table_before = _json14.dumps(_M1A3, sort_keys=True)
    _rr1 = reread_duration_bench(_td14)
    _after1 = _json14.loads(_bp14.read_text(encoding="utf-8"))
    _rr2 = reread_duration_bench(_td14)                   # idempotent
    _after2 = _json14.loads(_bp14.read_text(encoding="utf-8"))

    check("T14a the reading rule ignores a row under the sample floor, which retracts the 12.0 s "
          "that came from two windows; the re-read rewrites the verdict and never the table",
          # the floor itself
          MIN_N == 20
          # 20 is the smallest n at which ONE rejection is still inside the band; 19 is not
          and 1.0 / MIN_N <= MAX_FRR and 1.0 / (MIN_N - 1) > MAX_FRR
          and row_has_sample_floor(_solid) and not row_has_sample_floor(_sparse)
          and choose_min_embed_s([_sparse]) is None      # a zero on two windows never qualifies
          and choose_min_embed_s([_solid]) == _solid["d_s"]
          and choose_min_embed_s([_sparse, _solid]) == _solid["d_s"]
          # the measured table: the ONLY row meeting the bands is the one the floor excludes
          and [r["d_s"] for r in _bands_ok] == [12.0]
          and all(not row_has_sample_floor(r) for r in _bands_ok)
          and [r["d_s"] for r in _floor_ok] == [1.0, 2.0, 3.0, 5.0]
          and all(r["frr_at_stored"] > MAX_FRR for r in _floor_ok)
          and choose_min_embed_s(_M1A3) is None
          # and the retraction is caused by the floor and nothing else: at min_n=1 the OLD rule
          # reproduces 12.0 exactly
          and choose_min_embed_s(_M1A3, min_n=1) == min(r["d_s"] for r in _bands_ok) == 12.0
          # the re-read: verdict rewritten, value preserved as retracted, TABLE byte-identical
          and _rr1["before"] == 12.0 and _rr1["after"] is None and _rr1["retracted"] == 12.0
          and _after1["min_embed_s"] is None and _after1["retracted"] == 12.0
          and _after1["reading_rule"] == {"max_frr": MAX_FRR, "max_far": MAX_FAR, "min_n": MIN_N}
          and _json14.dumps(_after1["table"], sort_keys=True) == _table_before
          and _rr1["rows_under_floor"] == [8.0, 12.0]
          # re-running changes nothing further
          and _rr2["after"] is None and _after2 == _after1,
          str((choose_min_embed_s(_M1A3), choose_min_embed_s(_M1A3, min_n=1),
               [r["d_s"] for r in _bands_ok], [r["d_s"] for r in _floor_ok],
               _rr1["retracted"], _rr1["rows_under_floor"])))

print(f"\n{CHECKS - FAILS}/{CHECKS} checks passed")
sys.exit(1 if FAILS else 0)
