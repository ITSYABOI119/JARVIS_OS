#!/usr/bin/env python3
"""
Generate phase3/src/sel4/embed_gate_pairs.h from cm0_recall_set.json.

The C/M2b Leg-A box probe embeds BOTH sides of the measured recall pairs and
compares its cosines against the host reference (cm2b_pair_cos.json). For that to
be a comparison rather than a coincidence, the box must see the SAME strings — so
the table is generated from the eval set, never retyped.

Committed generator + generated header, the gen_embed_mu.py precedent, with a
`--check` drift gate so the header cannot silently fall behind the eval set.

PROBE-ONLY. The header self-guards on JARVIS_EMBED_PROBE, so a deployed build
contains none of it. That matters here: 72 strings is real .rodata, and it has no
business in an image that will never run the gate.

Usage:
    python3 phase3/scripts/embed/gen_recall_pairs.py           # write
    python3 phase3/scripts/embed/gen_recall_pairs.py --check   # drift gate
"""

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
RECALL = os.path.join(HERE, "cm0_recall_set.json")
HEADER = os.path.join(REPO, "phase3", "src", "sel4", "embed_gate_pairs.h")


def c_str(s):
    """C string literal. The eval set is plain ASCII; anything else is a hard error
    rather than a silent escape, because a mangled probe string would compare
    against the wrong host cosine and look like a pipeline discrepancy."""
    out = []
    for ch in s:
        o = ord(ch)
        if o < 0x20 or o > 0x7E:
            raise SystemExit(f"FAIL: non-ASCII/control byte {o!r} in {s!r}")
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def build():
    rs = json.loads(open(RECALL, encoding="utf-8").read())
    pairs = rs["distinct_positives"]
    longest = max(max(len(p["stored"]), len(p["later"])) for p in pairs)

    L = []
    A = L.append
    A("/*")
    A(" * JARVIS AI-OS - C/M2b Leg A: the MEASURED recall pairs, for the box probe.")
    A(" *")
    A(" * GENERATED FILE - DO NOT EDIT BY HAND.")
    A(" *   generator:  phase3/scripts/embed/gen_recall_pairs.py")
    A(" *   source:     phase3/scripts/embed/cm0_recall_set.json :: distinct_positives")
    A(" *   drift gate: gen_recall_pairs.py --check")
    A(" *   host ref:   phase3/scripts/embed/cm2b_pair_cos.json")
    A(" *")
    A(" * WHY GENERATED: Leg A compares the box's per-pair cosines against a host")
    A(" * reference computed over these exact strings. Retyping them would make any")
    A(" * disagreement ambiguous between 'the pipeline differs' and 'the inputs")
    A(" * differ', which is the one thing the gate must not be ambiguous about.")
    A(" *")
    A(" * PROBE-ONLY: self-guarded on JARVIS_EMBED_PROBE, so a deployed build carries")
    A(" * none of these strings.")
    A(" */")
    A("")
    A("#ifndef EMBED_GATE_PAIRS_H")
    A("#define EMBED_GATE_PAIRS_H")
    A("")
    A("#if JARVIS_EMBED_PROBE")
    A("")
    A(f"#define EMBED_GATE_NPAIRS   {len(pairs)}")
    A(f"#define EMBED_GATE_MAXLEN   {longest}   /* longest string, bytes (<< SHMEM_MAX_PAYLOAD 240) */")
    A("")
    A("typedef struct { const char *stored; const char *later; } embed_gate_pair_t;")
    A("")
    A("static const embed_gate_pair_t EMBED_GATE_PAIRS[EMBED_GATE_NPAIRS] = {")
    for p in pairs:
        A(f"    {{ {c_str(p['stored'])},")
        A(f"      {c_str(p['later'])} }},")
    A("};")
    A("")
    A("/* The pair invented for the first G2 attempt. NOT part of the measured set and")
    A(" * NOT counted in the fire rate - carried so the report can say where it sits")
    A(" * relative to the measured distribution. Host cosine: 0.5352 (below the floor). */")
    A('#define EMBED_GATE_EXTRA_STORED "what is a page fault"')
    A('#define EMBED_GATE_EXTRA_LATER  "explain what happens when a memory page is not resident"')
    A("")
    A("#endif /* JARVIS_EMBED_PROBE */")
    A("#endif /* EMBED_GATE_PAIRS_H */")
    return "\n".join(L) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    text = build()
    if a.check:
        if not os.path.exists(HEADER):
            print("FAIL: embed_gate_pairs.h missing - run without --check")
            return 1
        cur = open(HEADER, encoding="utf-8", newline="").read()
        if cur.replace("\r\n", "\n") != text.replace("\r\n", "\n"):
            print("FAIL: embed_gate_pairs.h is STALE vs cm0_recall_set.json")
            return 1
        print("OK: embed_gate_pairs.h matches cm0_recall_set.json")
        return 0
    open(HEADER, "w", newline="\n", encoding="utf-8").write(text)
    print(f"wrote {HEADER}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
