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

print(f"\n{CHECKS - FAILS}/{CHECKS} checks passed")
sys.exit(1 if FAILS else 0)
