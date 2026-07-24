#!/usr/bin/env python3
"""
cm1a_token_parity.py -- Phase C / C/M1a Stage 1: the TOKEN-PARITY gate.

Runs the compiled embed_tokenize_probe harness (the deployed C tokenizer on the Qwen3-Embedding-0.6B
GGUF vocab), reads its per-probe token-id output, and diffs it EXACTLY (no tolerance) against the
committed reference in golden_meta.json["token_ids"]. Reports PASS/FAIL per probe + the first-divergence
index on any mismatch, and exits 1 if any probe fails. TOKENIZATION ONLY (Stage 1) -- no vectors.

Model-gated (needs the 609 MB GGUF from the HF cache) -> NOT a CI step; reviewed via the reported output.

Run:  "$USERPROFILE/miniconda3/python.exe" phase3/scripts/embed/cm1a_token_parity.py <path-to-harness-exe>
      (GGUF path resolves from the HF cache via huggingface_hub; the harness exe is built with
       -DJARVIS_EMBED=1 -mavx2 -mfma -- see embed_tokenize_probe.c's header for the exact gcc line.)
"""
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
META = HERE / "golden_meta.json"
GGUF_REPO = "Qwen/Qwen3-Embedding-0.6B-GGUF"
GGUF_FILE = "Qwen3-Embedding-0.6B-Q8_0.gguf"


def first_divergence(a, b):
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            return i
    if len(a) != len(b):
        return min(len(a), len(b))
    return -1


def run_stress(harness, gguf):
    """Robustness measurement (NOT the gate): tokenize the committed cm1a_stress_strings.txt (realistic
    control-IN inputs — contractions/numbers/punctuation/code/non-ASCII/whitespace) and diff the C
    tokenizer vs llama.cpp references generated live. Reports divergences so the C/M2 pre-split fix (if
    any) has a verification set. Needs llama-cpp-python."""
    import llama_cpp
    from llama_cpp import Llama
    strings = [ln.rstrip("\r\n") for ln in (HERE / "cm1a_stress_strings.txt").read_text(
        encoding="utf-8").splitlines() if ln.strip()]
    llm = Llama(model_path=gguf, embedding=True, n_ctx=512,
                pooling_type=llama_cpp.LLAMA_POOLING_TYPE_LAST, verbose=False)
    eos = json.loads(META.read_text(encoding="utf-8"))["eos_appended"]
    ref = {s: list(llm.tokenize(s.encode("utf-8"), add_bos=False, special=False)) + [eos] for s in strings}
    sfile = HERE / "_stress_for_c.txt"
    sfile.write_text("\n".join(strings) + "\n", encoding="utf-8", newline="\n")
    out = subprocess.run([harness, gguf, str(sfile)], capture_output=True, text=True, encoding="utf-8")
    sfile.unlink(missing_ok=True)
    got = {}
    for line in out.stdout.splitlines():
        if "\t" in line:
            k, v = line.rsplit("\t", 1)
            try:
                got[k] = [int(x) for x in v.split(",") if x]
            except ValueError:
                pass
    npass, fails = 0, []
    for s in strings:
        if got.get(s) == ref[s]:
            npass += 1
        else:
            fails.append((s, got.get(s), ref[s]))
    print("\n== STRESS (robustness, NOT the gate): %d/%d match ==" % (npass, len(strings)))
    for s, h, w in fails:
        print("  DIVERGE %r" % s)
        print("      want:", w)
        print("      got :", h)
    return npass, len(strings)


def main():
    if len(sys.argv) < 2:
        print("usage: cm1a_token_parity.py <path-to-embed_tokenize_probe-exe> [--stress]")
        return 2
    harness = sys.argv[1]
    do_stress = "--stress" in sys.argv[2:]

    from huggingface_hub import hf_hub_download
    gguf = hf_hub_download(GGUF_REPO, GGUF_FILE)

    ref = json.loads(META.read_text(encoding="utf-8"))["token_ids"]

    out = subprocess.run([harness, gguf], capture_output=True, text=True, encoding="utf-8")
    if out.returncode != 0:
        print("HARNESS FAILED (rc=%d):\n%s" % (out.returncode, out.stderr[-2000:]))
        return 2
    if out.stderr.strip():
        print("[harness] " + out.stderr.strip().splitlines()[0])

    got = {}
    for line in out.stdout.splitlines():
        if "\t" not in line:
            continue
        probe, ids = line.split("\t", 1)
        got[probe] = [int(x) for x in ids.split(",") if x != ""]

    npass = 0
    fails = []
    for probe, want in ref.items():
        have = got.get(probe)
        if have == want:
            npass += 1
            print("  PASS  %-52s %s" % (probe[:52], want))
        else:
            div = first_divergence(have or [], want)
            fails.append((probe, have, want, div))
            print("  FAIL  %-52s" % probe[:52])
            print("        want[%2d]: %s" % (len(want), want))
            print("        got [%2d]: %s" % (len(have) if have is not None else -1, have))
            print("        first divergence at index %s" % div)

    print("\n== TOKEN PARITY (the GATE): %d/%d PASS ==" % (npass, len(ref)))
    if do_stress:
        run_stress(harness, gguf)
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
