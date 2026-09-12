#!/usr/bin/env python3
"""MS1b — the extractor bake-off: one model, every span, scored against the oracle.

    python3 phase7/memory/bench_ms1b.py --model gemma-e2b --households 10 --out <json>
    python3 phase7/memory/bench_ms1b.py --dry-run --households 1     # no server, no model

The model runs in a llama.cpp server this script starts and stops, one at a time. `--dry-run` prints
the request that would be sent for the first span and exits, which is what CI runs: it proves the
CLI imports, the registry-generated schema builds and a request is well formed, with no model on the
runner and no network.

Stdlib only. Nothing here reads the store; the extractor sees a span and its context, never a belief.
"""
import argparse
import hashlib
import json
import os
import sys
import time
import urllib.request as _urlreq
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from jarvis_memory.bench import corpus                      # noqa: E402
from jarvis_memory.extract import client as _client         # noqa: E402
from jarvis_memory.extract.derive import derive             # noqa: E402
from jarvis_memory.extract.schema import candidate_schema, schema_sha256   # noqa: E402
from jarvis_memory.extract import gguf_template as _tpl                    # noqa: E402
from jarvis_memory.extract.score import score_household, validity          # noqa: E402

# The contract the model was held to, recorded in every run's JSON beside the schema hash. Two runs
# are comparable only if BOTH agree. The three so far:
#   "wide"      L0 only - the model restated subject, speaker, source kind, object_norm and span ids
#   "narrow"    contract 1 - the decision set, one flat candidate object
#   "contract2" this one - the same decision set as a oneOf of four predicate-family branches, plus
#               the first-person `about` derivation in `derive`
# The earlier runs are KEPT under `_contract0` / `_contract1` suffixes and are never overwritten;
# `--verdict` refuses to mix contracts.
CONTRACT = "contract2"

# The FIELD (MS1b contract 2): every instruction model on hand that fits the RTX 2070 at 4-bit, plus
# the strongest fetchable ones and a purpose-built extractor. Same contract, same 2048-token budget,
# same temperature 0 / seed 1, same prompts, same rule — a model is added before its run, never
# after a number is seen. `thinking_switch` is true only where the chat template offers one (Qwen3 /
# Qwen3.5): those ran with thinking OFF so every model was measured doing the same job.
#
# CORRECTED 2026-09-12 (field addendum 2), and the correction is the reason addendum 2 exists: the
# line here used to read "Gemma 4's channel has no switch (the project's JARVIS_THINKING finding)
# and keeps the headroom instead". That is FALSE. Every Gemma 4 GGUF's chat template reads
# `enable_thinking` and emits `<|think|>` in the system turn when it is true, and both llama.cpp
# builds default `--reasoning auto`, which supplies `enable_thinking=true` whenever a request sends
# no kwarg — which these keys did not. Measured at the renderer over 167 of 167 prompts. So every
# Gemma run in both fields ran with thinking ON while the Qwen, Granite and NuExtract keys ran with
# it OFF. The premise came from the BOX's hand-built prompt template (`JARVIS_THINKING`) and was
# never checked against the Jinja engine that actually rendered these prompts.
#
# The rule that replaced it (the design §11, addendum 2): every run under one contract, budget,
# temperature, seed and venue is a legitimate configuration and its thinking state is part of its
# identity; a switchable model is measured in BOTH states so neither is assumed for it; the
# OFF-only reading is reported beside the choice; and a run's state is ASSERTED at the renderer
# before its arm starts (`render_expect`), never assumed from a comment like the one above.
# ORDER here is the queue's fastest-first order, so a winner can emerge before the slow tail.
MODELS = {
    # Every key declares four things beside its path, so a run's identity is readable from the table:
    #   `switch`        the template's thinking switch, measured at the renderer 2026-09-12, or None
    #   `expect_think`  the state the key's prompts RENDER ("on" / "off") - asserted, never assumed
    #   `render_expect` the marker spec the pre-flight checks against all 167 household-1 prompts
    #   `model_id`      the file-plus-template identity, so two states of one model can be paired
    # `think` appears only on addendum 2's twelve keys: it is the state they were ADDED to measure.
    # The pre-existing keys carry no `think` and behave exactly as they did when they ran.
    "llama-1b":   {"path": "phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf", "thinking_switch": False,
                   "switch": None, "expect_think": "off", "model_id": "llama-1b",
                   "render_expect": {"absent": ["<think>", "<|think|>"]}},
    "llama-3b":   {"path": "models/Llama-3.2-3B-Instruct-Q4_K_M.gguf",        "thinking_switch": False,
                   "switch": None, "expect_think": "off", "model_id": "llama-3b",
                   "render_expect": {"absent": ["<think>", "<|think|>"]}},
    "phi3-mini":  {"path": "models/Phi-3-mini-4k-instruct-q4.gguf",           "thinking_switch": False,
                   "switch": None, "expect_think": "off", "model_id": "phi3-mini",
                   "render_expect": {"absent": ["<think>", "<|think|>"]}},
    "qwen3-4b":   {"path": "models/Qwen3-4B-Q4_K_M.gguf",                     "thinking_switch": True,
                   "switch": "enable_thinking", "expect_think": "off", "model_id": "qwen3-4b-q4km",
                   "render_expect": {"tail_endswith": "<think>\n\n</think>\n\n"}},
    "qwen35-4b":  {"path": "models/Qwen3.5-4B-Q4_K_M.gguf",                   "thinking_switch": True,
                   "switch": "enable_thinking", "expect_think": "off", "model_id": "qwen35-4b-q4km",
                   "render_expect": {"tail_endswith": "<think>\n\n</think>\n\n"}},
    "phi4-mini":  {"path": "models/Phi-4-mini-instruct-Q4_K_M.gguf",          "thinking_switch": False,
                   "switch": None, "expect_think": "off", "model_id": "phi4-mini",
                   "render_expect": {"absent": ["<think>", "<|think|>"]}},
    "nuextract":  {"path": "models/NuExtract3-Q4_K_M.gguf",                   "thinking_switch": False,
                   "switch": "enable_thinking", "expect_think": "off", "model_id": "nuextract3-q4km",
                   "render_expect": {"tail_endswith": "<think>\n\n</think>\n\n"}},
    "llama-8b":   {"path": "models/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf",   "thinking_switch": False,
                   "switch": None, "expect_think": "off", "model_id": "llama-8b",
                   "render_expect": {"absent": ["<think>", "<|think|>"]}},
    "qwen3-8b":   {"path": "models/Qwen3-8B-Q4_K_M.gguf",                     "thinking_switch": True,
                   "switch": "enable_thinking", "expect_think": "off", "model_id": "qwen3-8b-q4km",
                   "render_expect": {"tail_endswith": "<think>\n\n</think>\n\n"}},
    "qwen35-9b":  {"path": "models/Qwen3.5-9B-Q4_K_M.gguf",                   "thinking_switch": True,
                   "switch": "enable_thinking", "expect_think": "off", "model_id": "qwen35-9b-q4km",
                   "render_expect": {"tail_endswith": "<think>\n\n</think>\n\n"}},
    "gemma-e2b":  {"path": "models/gemma-4-E2B-it-Q4_K_M.gguf",               "thinking_switch": False,
                   "switch": "enable_thinking", "expect_think": "on", "model_id": "gemma-e2b-q4km",
                   "render_expect": {"count": {"<|think|>": 1}}},
    "gemma-e4b":  {"path": "models/google_gemma-4-E4B-it-Q4_K_M.gguf",        "thinking_switch": False,
                   "switch": "enable_thinking", "expect_think": "on", "model_id": "gemma-e4b-q4km",
                   "render_expect": {"count": {"<|think|>": 1}}},

    # ---- the FIELD ADDENDUM (2026-09-10), pre-registered in the design (§11) before any of its
    # numbers. These run on llama.cpp v0.4.0 (build b10809) beside the frozen b8728 build, and the
    # verdict over them is computed WITHIN that build: two builds are never compared as one field.
    #
    # `gemma-e4b-v040` is the SAME FILE as `gemma-e4b`, re-run on the new build. It exists as its own
    # key so its result lands in its own JSON: the pair is the venue delta, and a venue delta needs
    # two runs, not one run relabelled.
    #
    # thinking_switch, read from each model's own chat template rather than assumed:
    #   granite-3b / granite-8b  `enable_thinking` DEFAULTS TO TRUE in Granite 4.2's template, so
    #                            leaving this False would have measured Granite with a thinking
    #                            channel against models without one - the exact unfairness the
    #                            field's rule exists to prevent.
    #   nuextract3               has `enable_thinking`, already defaulting to False; set explicitly.
    #   lfm25-2.6b               its template's `preserve_thinking` keeps PRIOR thinking in history
    #                            and is not a generation switch, so there is nothing to turn off -
    #                            it renders an OPEN `<think>` and thinks on every call regardless.
    #   ministral-8b             the INSTRUCT variant: no switch in the template at all.
    #   the three gemma arms     CORRECTED 2026-09-12 (addendum 2): the line here claimed Gemma 4's
    #                            channel has no switch. It has one - `enable_thinking` - and sending
    #                            no kwarg let llama.cpp's `--reasoning auto` default supply `true`,
    #                            so all three arms RENDERED `<|think|>` and thought. Their numbers
    #                            stand as the thinking-ON measurements they are; addendum 2 adds the
    #                            OFF states rather than re-basing them.
    #
    # NO key declares `extra_args`, and that is a MEASUREMENT rather than an omission. The addendum
    # anticipated that the 8.03 GB Q8_0 arm would need `--n-cpu-ffn` to fit an 8 GB card; on
    # v0.4.0 it does not, because `-ngl` now defaults to `auto` ("either an exact number, 'auto',
    # or 'all'") and the build fits the model itself. Measured at load, baseline 927 MiB:
    #   Q4_K_M  4241 MiB used, fully resident, 49.19 tok/s on a 40-token probe
    #   Q6_K    5123 MiB used, fully resident
    #   Q8_0    6111 MiB used, 2081 MiB free - loads and generates, and the 5.28 tok/s that probe
    #           reported did NOT hold on the workload: over the arm's own 1,670 calls the median is
    #           56.2 tok/s. The placement is split; the throughput cost is real but far smaller.
    # The consequence is throughput ONLY - the same weights are computed either side of the split -
    # and throughput is reported beside the verdict, never as the tie-breaker. The mechanism stays
    # (T42b) because the next model that needs a server argument should not have to invent it.
    "gemma-e4b-v040": {"path": "models/google_gemma-4-E4B-it-Q4_K_M.gguf",     "thinking_switch": False,
                       "switch": "enable_thinking", "expect_think": "on", "model_id": "gemma-e4b-q4km",
                       "render_expect": {"count": {"<|think|>": 1}}},
    "granite-3b":     {"path": "models/granite-4.2-3b-Q4_K_M.gguf",            "thinking_switch": True,
                       "switch": "enable_thinking", "expect_think": "off", "model_id": "granite-3b",
                       "render_expect": {"tail_endswith": "<think></think>"}},
    "granite-8b":     {"path": "models/granite-4.2-8b-Q4_K_M.gguf",            "thinking_switch": True,
                       "switch": "enable_thinking", "expect_think": "off", "model_id": "granite-8b",
                       "render_expect": {"tail_endswith": "<think></think>"}},
    "lfm25-2.6b":     {"path": "models/LFM2.5-2.6B-Q4_K_M.gguf",               "thinking_switch": False,
                       "switch": None, "expect_think": "on", "model_id": "lfm25-2.6b",
                       "render_expect": {"tail_endswith": "<think>"}},
    "ministral-8b":   {"path": "models/Ministral-3-8B-Instruct-2512-Q4_K_M.gguf", "thinking_switch": False,
                       "switch": None, "expect_think": "off", "model_id": "ministral-8b",
                       "render_expect": {"absent": ["<think>", "<|think|>"]}},
    "gemma-e4b-q6k":  {"path": "models/google_gemma-4-E4B-it-Q6_K.gguf",       "thinking_switch": False,
                       "switch": "enable_thinking", "expect_think": "on", "model_id": "gemma-e4b-q6k",
                       "render_expect": {"count": {"<|think|>": 1}}},
    "gemma-e4b-q8":   {"path": "models/google_gemma-4-E4B-it-Q8_0.gguf",       "thinking_switch": False,
                       "switch": "enable_thinking", "expect_think": "on", "model_id": "gemma-e4b-q8",
                       "render_expect": {"count": {"<|think|>": 1}}},
    "nuextract3":     {"path": "models/NuExtract3-Q4_K_M.gguf",                "thinking_switch": True,
                       "switch": "enable_thinking", "expect_think": "off", "model_id": "nuextract3-q4km",
                       "render_expect": {"tail_endswith": "<think>\n\n</think>\n\n"}},

    # ---- FIELD ADDENDUM 2 (2026-09-12), pre-registered in the design (§11) before any of its
    # numbers, and added because the addendum MEASURED that the field's fairness clause rested on a
    # false premise: every Gemma run had thought. Twelve arms, all on b10809, in the queue's
    # fastest-first order. Each declares the state it measures (`think`) and the marker its prompts
    # must RENDER (`render_expect`), asserted on the arm's own server before its first call.
    #
    # The two controlled Q8_0 arms answer the quantisation question at ONE variable: they load the
    # Q8_0 weights with the Q4_K_M file's own template (read from that GGUF at run time and passed
    # as `--chat-template-file`), so the only difference from arms 1 and `gemma-e4b-v040` is the
    # quantisation. `render_must_equal` names the reference digest they must match, 167 of 167.
    # `extra_args` stays undeclared: the template argument is RESOLVED at run time, never stored.
    "gemma-e4b-v040-nothink": {"path": "models/google_gemma-4-E4B-it-Q4_K_M.gguf", "thinking_switch": False,
                               "switch": "enable_thinking", "think": "off", "expect_think": "off",
                               "model_id": "gemma-e4b-q4km",
                               "render_expect": {"count": {"<|think|>": 0}}},
    "nuextract3-think":       {"path": "models/NuExtract3-Q4_K_M.gguf", "thinking_switch": False,
                               "switch": "enable_thinking", "think": "on", "expect_think": "on",
                               "model_id": "nuextract3-q4km",
                               "render_expect": {"tail_endswith": "<think>\n"}},
    "qwen3-8b-v040":          {"path": "models/Qwen3-8B-Q4_K_M.gguf", "thinking_switch": False,
                               "switch": "enable_thinking", "think": "off", "expect_think": "off",
                               "model_id": "qwen3-8b-q4km",
                               "render_expect": {"tail_endswith": "<think>\n\n</think>\n\n"}},
    "qwen35-4b-v040":         {"path": "models/Qwen3.5-4B-Q4_K_M.gguf", "thinking_switch": False,
                               "switch": "enable_thinking", "think": "off", "expect_think": "off",
                               "model_id": "qwen35-4b-q4km",
                               "render_expect": {"tail_endswith": "<think>\n\n</think>\n\n"}},
    "qwen35-9b-v040":         {"path": "models/Qwen3.5-9B-Q4_K_M.gguf", "thinking_switch": False,
                               "switch": "enable_thinking", "think": "off", "expect_think": "off",
                               "model_id": "qwen35-9b-q4km",
                               "render_expect": {"tail_endswith": "<think>\n\n</think>\n\n"}},
    "qwen3-8b-v040-think":    {"path": "models/Qwen3-8B-Q4_K_M.gguf", "thinking_switch": False,
                               "switch": "enable_thinking", "think": "on", "expect_think": "on",
                               "model_id": "qwen3-8b-q4km",
                               "render_expect": {"absent": ["<think>", "<|think|>"]}},
    # QWEN3.5 OPENS THE BLOCK, QWEN3 OMITS IT - measured at the renderer 2026-09-13, before either
    # arm ran, and read from the models' OWN templates rather than assumed from the Qwen3 one:
    #   Qwen3-8B    (template 57f1fd00f0013a2b): `{%- if enable_thinking is defined and
    #               enable_thinking is false %}` emits `<think>\n\n</think>\n\n`, and ON emits
    #               NOTHING - so its ON spec is `absent`, and its render passed that spec.
    #   Qwen3.5-4B and 9B (both 7f0e529032c25183, byte-identical to each other): `{%- if
    #               enable_thinking is defined and enable_thinking is true %}` emits `<think>\n` -
    #               the opposite convention. The `absent` spec declared for them was WRONG and its
    #               render said so, 167 of 167, which is what the pre-flight exists to do.
    # The spec below is set FROM THE TEMPLATE, before these arms run and before any number of theirs
    # exists - not a threshold moved after a result.
    "qwen35-4b-v040-think":   {"path": "models/Qwen3.5-4B-Q4_K_M.gguf", "thinking_switch": False,
                               "switch": "enable_thinking", "think": "on", "expect_think": "on",
                               "model_id": "qwen35-4b-q4km",
                               "render_expect": {"tail_endswith": "<think>\n"}},
    "qwen35-9b-v040-think":   {"path": "models/Qwen3.5-9B-Q4_K_M.gguf", "thinking_switch": False,
                               "switch": "enable_thinking", "think": "on", "expect_think": "on",
                               "model_id": "qwen35-9b-q4km",
                               "render_expect": {"tail_endswith": "<think>\n"}},
    "gemma-e2b-v040-nothink": {"path": "models/gemma-4-E2B-it-Q4_K_M.gguf", "thinking_switch": False,
                               "switch": "enable_thinking", "think": "off", "expect_think": "off",
                               "model_id": "gemma-e2b-q4km",
                               "render_expect": {"count": {"<|think|>": 0}}},
    "gemma-e2b-v040":         {"path": "models/gemma-4-E2B-it-Q4_K_M.gguf", "thinking_switch": False,
                               "switch": "enable_thinking", "think": "on", "expect_think": "on",
                               "model_id": "gemma-e2b-q4km",
                               "render_expect": {"count": {"<|think|>": 1}}},
    "gemma-e4b-q8-q4tpl-nothink": {"path": "models/google_gemma-4-E4B-it-Q8_0.gguf", "thinking_switch": False,
                                   "switch": "enable_thinking", "think": "off", "expect_think": "off",
                                   "model_id": "gemma-e4b-q8-q4tpl",
                                   "template_from": "models/google_gemma-4-E4B-it-Q4_K_M.gguf",
                                   "render_must_equal": "gemma-e4b-v040-nothink",
                                   "render_expect": {"count": {"<|think|>": 0}}},
    "gemma-e4b-q8-q4tpl":     {"path": "models/google_gemma-4-E4B-it-Q8_0.gguf", "thinking_switch": False,
                               "switch": "enable_thinking", "think": None, "expect_think": "on",
                               "model_id": "gemma-e4b-q8-q4tpl",
                               "template_from": "models/google_gemma-4-E4B-it-Q4_K_M.gguf",
                               "render_must_equal": "gemma-e4b-v040",
                               "render_expect": {"count": {"<|think|>": 1}}},
}


# The contract-2 schema, as the eleven-model field measured it. Asserted before a queue starts so a
# tree carrying a LATER contract (MS2's contract 3) cannot silently produce runs that would be
# compared against contract-2 numbers. The hash is the schema's, not the file's.
CONTRACT2_SCHEMA_SHA256 = "846ad08857eb37f8175f0aa24fbbfdfe2c7edf89c5565b2521209a91792dbc49"


def contract2_schema_ok(schema_hash) -> bool:
    """Is this the schema the field ran under? Pure, so the refusal is testable without a server."""
    return schema_hash == CONTRACT2_SCHEMA_SHA256


def build_matches(version, required) -> bool:
    """Is `required` a substring of the build string a server reported? Pure.

    Substring, not equality, because the string a server reports carries its commit
    (`b10809-5266f24da`) and the thing being asserted is the BUILD. An empty or absent requirement
    matches anything - the frozen field ran before any of this existed and must still read.
    """
    if not required:
        return True
    return required in (version or "")


def model_path(key):
    return MODELS[key]["path"]


def thinking_switch(key):
    """True when the request must send `enable_thinking: false`.

    A key that DECLARES `think` says which state it was added to measure, and that declaration
    wins: `"off"` sends the kwarg, `"on"` does not (the ON kwarg is built from the key's own
    `think` field). A key without `think` keeps the legacy flag exactly as its run used it, so no
    pre-existing run's meaning moves.
    """
    m = MODELS[key]
    if m.get("think") is not None:
        return m["think"] == "off"
    return bool(m["thinking_switch"])


def model_extra_args(key):
    """The per-model server arguments, or []. Declared in MODELS beside the model they belong to."""
    return list(MODELS[key].get("extra_args") or [])


def model_think(key):
    """The thinking state a key declares - `"on"`, `"off"`, or None for the pre-existing keys."""
    return MODELS[key].get("think")


def model_think_extra(key):
    """Extra chat-template kwargs a key needs beside the switch (NuExtract3's `mode`), or {}."""
    return dict(MODELS[key].get("think_extra") or {})


def resolved_extra_args(key, template_path=None):
    """`model_extra_args` plus `--chat-template-file` when a key borrows another file's template.

    The controlled Q8_0 arms load Q8_0 weights with the Q4_K_M file's template, so the ONLY
    difference from the Q4 arms is the quantisation. The path is resolved at run time by reading
    the template out of the pinned GGUF, never stored in the table - so `extra_args` stays empty on
    every key and T42b's emptiness pin keeps meaning what it says.
    """
    args = model_extra_args(key)
    if MODELS[key].get("template_from"):
        if not template_path:
            raise ValueError("%s declares template_from and needs a resolved template path" % key)
        args = args + ["--chat-template-file", str(template_path)]
    return args


def _sha256(path, budget=None):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        while True:
            b = fh.read(1 << 20)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def _readable(cand, span_text):
    """One failure as `span text -> predicate subject = value`, for the log and the report."""
    subj = cand.get("subject") or {}
    sids = cand.get("span_ids") or []
    text = "; ".join(str(span_text.get(s, "?")) for s in sids) or "(no span)"
    value = cand.get("relation_id") if cand.get("predicate_id") == "person.relation_to" \
        else cand.get("object_norm")
    return "%s -> %s %s:%s = %s [%s]" % (text, cand.get("predicate_id"), subj.get("kind"),
                                         subj.get("ref"), value, cand.get("source_kind"))


class RenderMismatch(RuntimeError):
    """What a server RENDERED is not the state the key declared.

    A STOP, never a spec edit: the point of declaring `render_expect` before a run is that the
    declaration can be wrong, and finding that out is a finding. `run_queue` stops on it.
    """


def render_matches(prompt, spec) -> bool:
    """Does one rendered prompt satisfy a marker spec? Pure.

    Three clause kinds, and a spec may combine them - ALL must hold:
      `count`         {marker: the exact number of occurrences in the whole prompt}
      `tail_endswith` the prompt ends with this string (the generation prompt is the tail)
      `absent`        none of these substrings occurs anywhere
    """
    if not spec:
        return False
    for marker, want in (spec.get("count") or {}).items():
        if prompt.count(marker) != want:
            return False
    tail = spec.get("tail_endswith")
    if tail is not None and not prompt.endswith(tail):
        return False
    for marker in (spec.get("absent") or []):
        if marker in prompt:
            return False
    return True


def render_digest(prompts) -> dict:
    """A per-prompt fingerprint plus one over the whole set.

    The per-prompt list is what makes `render_must_equal` a real check: the controlled Q8_0 arms
    must render the SAME 167 prompts as their Q4 references, and a digest of the concatenation
    alone could not say WHICH prompt differed.
    """
    hashes = [hashlib.sha256(p.encode("utf-8")).hexdigest() for p in prompts]
    joined = hashlib.sha256("\x00".join(prompts).encode("utf-8")).hexdigest()
    return {"n": len(prompts), "prompt_sha256": hashes, "all": joined}


def queue_action_for(exc) -> str:
    """What the queue does with a failed arm: stop the whole run, or skip this one.

    A RenderMismatch means the field's own declaration is wrong, so every later arm's meaning is in
    doubt and the queue STOPS. Anything else is one arm's problem, skipped with its reason.
    """
    return "stop" if isinstance(exc, RenderMismatch) else "skip"


def _props_template_sha(base_url):
    """The sha256 of the chat template the SERVER says it is using, or None."""
    try:
        with _urlreq.urlopen(base_url.rstrip("/") + "/props", timeout=10) as fh:
            props = json.loads(fh.read().decode("utf-8"))
        return _tpl.template_sha256(props.get("chat_template") or "")
    except Exception:                                              # noqa: BLE001 - recorded, not fatal
        return None


def render_prompts(base_url, key, seed, days, max_tokens=2048):
    """The prompts a server would decode for one household, through its OWN chat template.

    POSTs exactly the body `build_request` will send for this key to `/apply-template`, so the
    thing asserted is the thing that will run - never a re-implementation of it.
    """
    hh = corpus.generate_household(seed, days)
    names, _ = _household_context(hh)
    schema = candidate_schema()
    out = []
    for sp in hh["spans"]:
        body = _client.build_request(sp["text"], sp["cluster"], sp["day"], names, sp["sid"], schema,
                                     max_tokens=max_tokens, thinking_switch=thinking_switch(key),
                                     think=model_think(key), think_extra=model_think_extra(key))
        req = _urlreq.Request(base_url.rstrip("/") + "/apply-template",
                              data=json.dumps(body).encode("utf-8"),
                              headers={"Content-Type": "application/json"})
        with _urlreq.urlopen(req, timeout=60) as fh:
            out.append(json.loads(fh.read().decode("utf-8"))["prompt"])
    return out


def resolve_template(key):
    """(path, sha256) of the template a key borrows from another GGUF, or (None, None).

    Read from the pinned file at run time. The sha256 goes into the run JSON so a reader can tell
    WHICH template rendered a run without re-deriving it.
    """
    src = MODELS[key].get("template_from")
    if not src:
        return None, None
    text = _tpl.read_chat_template(src)
    if not text:
        raise RuntimeError("%s declares template_from %s, which carries no chat template"
                           % (key, src))
    return _tpl.write_template_file(text), _tpl.template_sha256(text)


def _household_context(hh):
    """The names the contract allows the extractor to see: the owner's and the partner's."""
    names_by_cluster, clusters_by_name = {}, {}
    for i, p in enumerate(hh["persons"][:2], start=1):
        names_by_cluster[i] = p["name"]
        clusters_by_name[str(p["name"]).lower()] = i
    return names_by_cluster, clusters_by_name


def run_model(model_key, seeds, days, out_path, port, ctx, ngl, max_tokens, require_build=None):
    mpath = model_path(model_key)
    think = thinking_switch(model_key)
    think_state = model_think(model_key)
    think_extra = model_think_extra(model_key)
    schema = candidate_schema()
    households, all_results = [], []
    t_start = time.time()
    tpl_path, tpl_from_sha = resolve_template(model_key)
    with _client.LlamaServer(mpath, port=port, ctx=ctx, ngl=ngl,
                             extra_args=resolved_extra_args(model_key, tpl_path)) as srv:
        print("server up: %s  (%s)" % (srv.base_url, srv.version))
        # The venue check, made in the first seconds of an arm rather than after its hours: an
        # addendum run that started against the frozen build because JARVIS_LLAMA_BIN was not set
        # would produce a perfectly valid-looking number belonging to the wrong field.
        if not build_matches(srv.version, require_build):
            raise RuntimeError(
                "server reports build %r, which does not contain %r - refusing to run %s here; "
                "set JARVIS_LLAMA_BIN to the intended llama.cpp bin directory"
                % (srv.version, require_build, model_key))
        # THE RENDER PRE-FLIGHT, before the first call: what this server will actually decode from,
        # asserted against the state the key declares. A mismatch raises RenderMismatch, which stops
        # the queue - it is a finding about the declaration, never a reason to edit the spec.
        spec = MODELS[model_key].get("render_expect")
        prompts = render_prompts(srv.base_url, model_key, seeds[0], days, max_tokens)
        bad = [i for i, p in enumerate(prompts) if not render_matches(p, spec)]
        if bad:
            raise RenderMismatch(
                "%s rendered %d of %d prompts against spec %r; first offender #%d ends: %r"
                % (model_key, len(bad), len(prompts), spec, bad[0], prompts[bad[0]][-200:]))
        digest = render_digest(prompts)
        must = MODELS[model_key].get("render_must_equal")
        if must:
            ref_path = os.path.join(RESULTS_DIR, "render_digest_%s.json" % must)
            if not os.path.exists(ref_path):
                raise RenderMismatch("%s must render identically to %s, whose digest %s does not "
                                     "exist - render it first" % (model_key, must, ref_path))
            with open(ref_path, encoding="utf-8") as fh:
                ref = json.load(fh)
            ref_hashes = ref.get("prompt_sha256") or []
            same = sum(1 for a, b in zip(digest["prompt_sha256"], ref_hashes) if a == b)
            if same != digest["n"] or digest["n"] != len(ref_hashes):
                raise RenderMismatch(
                    "%s renders %d of %d prompts identically to %s - the controlled comparison is "
                    "not controlled" % (model_key, same, digest["n"], must))
        tpl_sha = tpl_from_sha or _props_template_sha(srv.base_url)
        print("  render ok: %d prompts, state %s, template %s"
              % (digest["n"], MODELS[model_key].get("expect_think"), (tpl_sha or "?")[:16]))
        for seed in seeds:
            hh = corpus.generate_household(seed, days)
            names, clusters_by_name = _household_context(hh)
            span_cluster = {s["sid"]: s["cluster"] for s in hh["spans"]}
            span_text = {s["sid"]: s["text"] for s in hh["spans"]}
            preds, results = [], []
            t0 = time.time()
            for s in hh["spans"]:
                r = _client.extract_span(srv.base_url, s["sid"], s["text"], s["cluster"],
                                         s["day"], names, schema, max_tokens=max_tokens,
                                         thinking_switch=think, think=think_state,
                                         think_extra=think_extra)
                raw = r.get("candidates")
                if raw is not None:
                    # DERIVE before anything scores it. Validity is measured on the candidate that
                    # would actually reach the store, not on the model's half of it, and the raw
                    # decision set is kept beside it so a failure can be read back to what the
                    # model said rather than to what the code made of it.
                    r["raw_candidates"] = list(raw)
                    r["candidates"] = [derive(c, s, owner_cluster=1, names=names) for c in raw]
                results.append(r)
                for c in (r.get("candidates") or []):
                    preds.append(dict(c))
            valid, total, reasons = validity(results, span_cluster)
            sc = score_household(preds, hh["candidates"], names, clusters_by_name)
            sc.update({
                "seed": seed,
                "n_spans": len(hh["spans"]),
                "valid_calls": valid, "total_calls": total,
                "validity": round(valid / total, 4) if total else 0.0,
                "invalid_reasons": reasons,
                # The OUTPUT side of the thinking state: how many calls came back carrying a
                # reasoning_content. The render check says what went IN; this says what came BACK.
                "reasoning_calls": sum(1 for r in results if r.get("reasoning_present")),
                "seconds": round(time.time() - t0, 1),
                "tokens_in": sum(r.get("tokens_in", 0) for r in results),
                "tokens_out": sum(r.get("tokens_out", 0) for r in results),
                "false_positives": [preds[i] for i in sc["false_positive_idx"][:50]],
                "false_negatives": [hh["candidates"][j] for j in sc["false_negative_idx"][:50]],
                # The readable form the report quotes: the utterance, then what was said about it
                # versus what the oracle holds. A bare candidate dict cannot be judged without the
                # sentence it came from, and judging the failures is the whole point of the run.
                "fp_readable": [_readable(preds[i], span_text) for i in
                                sc["false_positive_idx"][:10]],
                "fn_readable": [_readable(hh["candidates"][j], span_text) for j in
                                sc["false_negative_idx"][:10]],
                "invalid_raw": [r["raw"][:600] for r in results
                                if r.get("candidates") is None][:50],
                "predictions": preds,
            })
            households.append(sc)
            all_results.extend(results)
            print("  seed %2d: validity %.4f  P %.4f R %.4f F1 %.4f  (%d preds / %d gold, %.1f s)"
                  % (seed, sc["validity"], sc["precision"], sc["recall"], sc["f1"],
                     sc["n_pred"], sc["n_gold"], sc["seconds"]))
        version = srv.version

    tot_valid = sum(h["valid_calls"] for h in households)
    tot_calls = sum(h["total_calls"] for h in households)
    tot_match = sum(h["n_match"] for h in households)
    tot_pred = sum(h["n_pred"] for h in households)
    tot_gold = sum(h["n_gold"] for h in households)
    lm = sum(round(h["lenient_recall"] * h["n_gold"]) for h in households)
    p = tot_match / tot_pred if tot_pred else 0.0
    r = tot_match / tot_gold if tot_gold else 0.0
    lp = lm / tot_pred if tot_pred else 0.0
    lr = lm / tot_gold if tot_gold else 0.0
    agg = {
        "validity": round(tot_valid / tot_calls, 4) if tot_calls else 0.0,
        "precision": round(p, 4), "recall": round(r, 4),
        "f1": round((2 * p * r / (p + r)) if (p + r) else 0.0, 4),
        "lenient_f1": round((2 * lp * lr / (lp + lr)) if (lp + lr) else 0.0, 4),
        "relation_recall": round(sum(h["relation_matched"] for h in households)
                                 / max(1, sum(h["relation_gold"] for h in households)), 4),
        # Split, never averaged: the stated half is extraction, the inferred half is the people
        # layer's job and MS1b has no people layer.
        "relation_stated_recall": round(
            sum(h["relation_stated_matched"] for h in households)
            / max(1, sum(h["relation_stated_gold"] for h in households)), 4),
        "relation_inferred_recall": round(
            sum(h["relation_inferred_matched"] for h in households)
            / max(1, sum(h["relation_inferred_gold"] for h in households)), 4),
        "relation_stated_gold": sum(h["relation_stated_gold"] for h in households),
        "relation_inferred_gold": sum(h["relation_inferred_gold"] for h in households),
        "preference_polarity_agreement": round(
            sum(h["preference_polarity_agreement"] for h in households) / len(households), 4),
        "n_pred": tot_pred, "n_gold": tot_gold, "n_match": tot_match,
        # REPORTED beside the band, never in it: F1 over the gold a per-span extractor can reach
        # (the inferred edges removed from the recall denominator only), and the predictions on
        # predicates with no gold anywhere in this corpus.
        "scorable_gold": sum(h["scorable_gold"] for h in households),
        "f1_scorable": round(
            (lambda P, R: (2 * P * R / (P + R)) if (P + R) else 0.0)(
                p, tot_match / max(1, sum(h["scorable_gold"] for h in households))), 4),
        "zero_gold_predictions": sum(h["zero_gold_predictions"] for h in households),
        "reasoning_calls": sum(h.get("reasoning_calls", 0) for h in households),
        "valid_calls": tot_valid, "total_calls": tot_calls,
        "tokens_in": sum(h["tokens_in"] for h in households),
        "tokens_out": sum(h["tokens_out"] for h in households),
        "seconds": round(time.time() - t_start, 1),
    }
    reasons = {}
    for h in households:
        for k, v in h["invalid_reasons"].items():
            reasons[k] = reasons.get(k, 0) + v

    out = {
        "model_key": model_key,
        "model_path": mpath,
        "model_bytes": os.path.getsize(mpath),
        "model_sha256": _sha256(mpath),
        "thinking_switch_applied": think,
        # The state this run RAN in, recorded so no later reader has to infer it from a comment:
        # what was requested, what the renderer confirmed, and which template did the rendering.
        "think_requested": think_state,
        "think_extra": think_extra or None,
        "think_rendered": MODELS[model_key].get("expect_think"),
        "chat_template_sha256": tpl_sha,
        "render_digest_all": digest["all"],
        "llama_version": version,
        "schema_sha256": schema_sha256(),
        "contract": CONTRACT,
        "days": days, "seeds": list(seeds), "max_tokens": max_tokens,
        "aggregate": agg,
        "invalid_reasons_total": reasons,
        "households": households,
        "scope": ("MS1b: synthetic seeded utterances, ORACLE candidates as the gold standard, "
                  "schema-constrained JSON through llama.cpp. Nothing measured on real speech or "
                  "on the owner."),
    }
    if out_path:
        with open(out_path, "w", encoding="utf-8") as fh:
            json.dump(out, fh, indent=2, sort_keys=True)
            fh.write("\n")
    return out


# The pre-registered rule, in code so the verdict is applied rather than argued (design §11, the
# MS1 row): a model qualifies at validity >= 99 %, must clear the 0.60 F1 floor, and the highest F1
# among the qualifiers is CHOSEN. If none clears the floor NOTHING is chosen and the ceiling is
# recorded. Written before either run of the narrow contract; a MISS is a MISS.
VALIDITY_BAND = 0.99
F1_FLOOR = 0.60


def verdict(runs) -> dict:
    """runs: [(label, aggregate)] -> the pre-registered decision, with its reason."""
    rows = [(lab, ag.get("validity", 0.0), ag.get("f1", 0.0)) for lab, ag in runs]
    qualified = [r for r in rows if r[1] >= VALIDITY_BAND and r[2] >= F1_FLOOR]
    best = max(rows, key=lambda r: r[2]) if rows else None
    if qualified:
        win = max(qualified, key=lambda r: r[2])
        return {"chosen": win[0], "reason": "validity %.4f >= %.2f and F1 %.4f >= %.2f, the highest"
                % (win[1], VALIDITY_BAND, win[2], F1_FLOOR), "rows": rows}
    return {"chosen": None,
            "reason": ("none reached the %.2f F1 floor at validity >= %.2f - ceiling F1 %.4f (%s)"
                       % (F1_FLOOR, VALIDITY_BAND, best[2], best[0]) if best else "no runs"),
            "rows": rows}


RESULTS_DIR = str(Path(__file__).resolve().parent / "bench" / "results")


def load_field(results_dir=None, contract=None, build=None):
    """Every CONTRACT-2 run in the results dir, as [(key, aggregate, path)].

    `build`, when given, keeps only runs whose recorded `llama_version` contains it. Numbers are
    comparable only within a build (the design's §10): the eleven-model field ran on a 2026-04-09
    checkout (`b8728-...`) and the addendum on v0.4.0 (`b10809-...`), and a verdict computed over
    both would be a table of two different venues read as one.

    Four exclusions, each for its own reason:
      * `*_contract0.json` / `*_contract1.json` — superseded runs, KEPT on purpose and never mixed
        into a verdict with the contract they were superseded by;
      * `*_run1.json` — the superseded FIRST run of an arm that was re-run, kept beside its
        replacement instead of being overwritten. The suffix is deliberately loud rather than a
        hidden folder: a reader of the results directory sees that an arm has two runs and why.
        (`gemma-e4b-q8`, 2026-09-12: the provenance read taken at the end of run 1 was
        untrustworthy, so the arm was re-run on the verified file and run 1 was renamed.)
      * files with no `model_key` — `ms1b_field_verdict.json` and `ms1b_store_on_extracted.json` are
        outputs of this milestone, not model runs, and a verdict that tried to read its own previous
        output would be circular;
      * a real run whose `contract` label is anything else RAISES with its filename. Silently
        skipping it would let a stale run vanish from a field it belongs in; averaging it in would
        compare models measured under different rules. Loud is the only safe option.
    """
    results_dir = results_dir or RESULTS_DIR
    contract = contract or CONTRACT
    out = []
    for path in sorted(Path(results_dir).glob("ms1b_*.json")):
        name = path.name
        if (name.endswith("_contract0.json") or name.endswith("_contract1.json")
                or name.endswith("_run1.json")):
            continue
        with open(path, encoding="utf-8") as fh:
            d = json.load(fh)
        if "model_key" not in d:
            continue
        if d.get("contract") != contract:
            raise ValueError("%s is labelled %r, not %r - a verdict never mixes contracts"
                             % (name, d.get("contract"), contract))
        if not build_matches(d.get("llama_version"), build):
            continue
        out.append((d["model_key"], d.get("aggregate", {}), str(path)))
    return out


def eligible_offonly(run_json, key_table=None) -> bool:
    """Is this run part of the OFF-ONLY READING - the field as the old clause intended it?

    Eligible when the run did not think BY CHOICE: either its key has no switch at all (a model
    that thinks unconditionally, or does not think, cannot be asked to stop), or the state it
    rendered is "off". A run records `think_rendered`; a run from before that field existed is
    judged by its key's declared `expect_think`, which the addendum measured at the renderer.

    This reading is REPORTED beside the choice and never replaces it: every run under one contract,
    budget and venue is a legitimate configuration, and its thinking state is part of its identity.
    """
    table = key_table if key_table is not None else MODELS
    key = run_json.get("model_key")
    entry = table.get(key, {}) if key else {}
    if entry.get("switch") is None:
        return True
    state = run_json.get("think_rendered") or entry.get("expect_think")
    return state == "off"


def operating_key(chosen_key, runs, key_table=None, band=0.01):
    """The configuration MS2 runs: the chosen model's OFF state unless ON is worth more than `band`.

    `runs` is [(key, aggregate)]. Among the runs sharing the chosen key's `model_id`, the OFF-state
    run wins unless the ON-state run beats it by MORE than the band - which is set below both the
    measured venue delta (0.0269) and the frozen field's winning margin (0.0225), so a difference
    inside it is not large enough to buy thinking's cost. With one state present, the chosen key is
    the operating key and says so.
    """
    table = key_table if key_table is not None else MODELS
    chosen_id = (table.get(chosen_key) or {}).get("model_id")
    if not chosen_id:
        return chosen_key, "the chosen key declares no model_id, so it is its own operating state"
    f1 = {}
    for key, agg in runs:
        entry = table.get(key) or {}
        if entry.get("model_id") != chosen_id:
            continue
        state = entry.get("expect_think")
        if state in ("on", "off"):
            # newest wins is meaningless here; two runs of one state would be a duplicate, and the
            # highest F1 of a state is the one that state achieved
            f1[state] = max(f1.get(state, -1.0), float(agg.get("f1") or 0.0))
    if "on" in f1 and "off" in f1:
        if f1["on"] - f1["off"] > band:
            on_key = _state_key(chosen_id, "on", table)
            return on_key, ("ON beats OFF by %.4f, more than the %.2f band (%.4f vs %.4f)"
                            % (f1["on"] - f1["off"], band, f1["on"], f1["off"]))
        off_key = _state_key(chosen_id, "off", table)
        return off_key, ("OFF stands: ON beats it by %.4f, within the %.2f band (%.4f vs %.4f)"
                         % (f1["on"] - f1["off"], band, f1["on"], f1["off"]))
    only = "on" if "on" in f1 else ("off" if "off" in f1 else None)
    return chosen_key, ("only the %s state was measured for %s" % (only, chosen_id) if only
                        else "no state was measured for %s" % chosen_id)


def _state_key(model_id, state, table):
    """The key of `model_id` in `state` - the run's own key, so the report names what ran."""
    for key, entry in table.items():
        if entry.get("model_id") == model_id and entry.get("expect_think") == state:
            return key
    return None


def _queue_log(log_path, line):
    print(line, flush=True)
    if log_path:
        with open(log_path, "a", encoding="utf-8") as fh:
            fh.write(line + "\n")


def run_queue(keys, seeds, days, log_path, port, ctx, ngl, max_tokens, results_dir=None,
              require_build=None, schema_hash=None):
    """Run the field sequentially, ONE server at a time, resumable.

    Idempotent by design because the queue runs for many hours and a session may end under it: a key
    already finished at THIS contract and schema hash is skipped, a key whose JSON was written under
    a different contract STOPS the queue rather than being silently overwritten, and a model file
    that is not on disk is skipped with the reason logged and no JSON created.
    """
    results_dir = results_dir or RESULTS_DIR
    schema_hash = schema_sha256() if schema_hash is None else schema_hash
    # BEFORE anything else, including the resume scan: a tree carrying a later contract's schema
    # would produce runs that look like field members and are not comparable to one.
    if not contract2_schema_ok(schema_hash):
        raise SystemExit(
            "the tree's candidate schema hashes to %s, not the contract-2 schema %s the field ran "
            "under - refusing to queue. If a later contract is in the tree (MS2's contract 3), this "
            "run must wait for it to be pinned back, or its numbers would be compared across "
            "contracts." % (schema_hash, CONTRACT2_SCHEMA_SHA256))
    done, skipped = [], []
    for key in keys:
        out_path = os.path.join(results_dir, "ms1b_%s.json" % key)
        if os.path.exists(out_path):
            with open(out_path, encoding="utf-8") as fh:
                prev = json.load(fh)
            if prev.get("contract") == CONTRACT and prev.get("schema_sha256") == schema_hash:
                _queue_log(log_path, "%s SKIP already done (F1 %.4f)"
                           % (key, prev.get("aggregate", {}).get("f1", 0.0)))
                done.append(key)
                continue
            _queue_log(log_path, "%s STOP existing JSON is contract=%r schema=%s - refusing to "
                                 "overwrite; rename it by hand if it is superseded"
                       % (key, prev.get("contract"), str(prev.get("schema_sha256"))[:12]))
            return done, skipped, key
        if key not in MODELS:
            _queue_log(log_path, "%s SKIP unknown key" % key)
            skipped.append((key, "unknown key"))
            continue
        mpath = model_path(key)
        if not os.path.exists(mpath):
            _queue_log(log_path, "%s SKIP model file absent: %s" % (key, mpath))
            skipped.append((key, "model file absent: %s" % mpath))
            continue
        _queue_log(log_path, "%s START %s think=%s args=%s"
                   % (key, time.strftime("%Y-%m-%d %H:%M:%S"), thinking_switch(key),
                      " ".join(model_extra_args(key)) or "-"))
        t0 = time.time()
        try:
            res = run_model(key, seeds, days, out_path, port, ctx, ngl, max_tokens,
                            require_build=require_build)
        except Exception as exc:                                   # noqa: BLE001
            if queue_action_for(exc) == "stop":
                _queue_log(log_path, "%s STOP render mismatch %s" % (key, exc))
                return done, skipped, key
            _queue_log(log_path, "%s ERROR %s" % (key, exc))
            skipped.append((key, "error: %s" % exc))
            continue
        ag = res["aggregate"]
        _queue_log(log_path, "%s END %s validity %.4f F1 %.4f scorableF1 %.4f %.1f s think=%s"
                   % (key, time.strftime("%Y-%m-%d %H:%M:%S"), ag["validity"], ag["f1"],
                      ag.get("f1_scorable", 0.0), time.time() - t0, thinking_switch(key)))
        done.append(key)
    return done, skipped, None


def run_smoke(key, days, port, ctx, ngl, max_tokens):
    """Two real calls against one model: does the contract-2 schema compile and answer?

    A 400 means llama.cpp could not turn the schema into a grammar, which is the one failure that
    would silently invalidate an entire overnight queue - so it is checked on the cheapest model
    before the field runs, and it is a STOP, never a fallback to the flat schema.
    """
    schema = candidate_schema()
    hh = corpus.generate_household(1, days)
    names, _ = _household_context(hh)
    picks = []
    for prefix, cluster in (("i work as", None), ("my husband", 2)):
        for s in hh["spans"]:
            if s["text"].startswith(prefix) and (cluster is None or s["cluster"] == cluster):
                picks.append(s)
                break
    if len(picks) < 2:
        print("smoke: could not find both probe spans (%d found)" % len(picks))
        return 1
    ok = True
    with _client.LlamaServer(model_path(key), port=port, ctx=ctx, ngl=ngl,
                             extra_args=model_extra_args(key)) as srv:
        print("server up: %s  (%s)" % (srv.base_url, srv.version))
        for s in picks:
            r = _client.extract_span(srv.base_url, s["sid"], s["text"], s["cluster"], s["day"],
                                     names, schema, max_tokens=max_tokens,
                                     thinking_switch=thinking_switch(key))
            cands = r.get("candidates")
            print("--- span %d cluster %d: %r" % (s["sid"], s["cluster"], s["text"]))
            print("    http=%s finish=%s" % (r.get("status", "?"), r.get("finish_reason", "?")))
            print("    raw=%s" % json.dumps(cands)[:400] if cands is not None
                  else "    raw=UNPARSED %s" % str(r.get("raw"))[:300])
            if cands is None:
                ok = False
                continue
            for c in cands:
                d = derive(c, s, owner_cluster=1, names=names)
                print("    derived subject=%s source=%s object_norm=%r predicate=%s"
                      % (d["subject"], d["source_kind"], d["object_norm"], d["predicate_id"]))
    return 0 if ok else 1


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", choices=sorted(MODELS), default="gemma-e2b")
    ap.add_argument("--households", type=int, default=10)
    ap.add_argument("--days", type=int, default=14)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--out", default=None)
    ap.add_argument("--dry-run", action="store_true",
                    help="print the request for the first span and exit; no server, no model")
    ap.add_argument("--port", type=int, default=8089)
    ap.add_argument("--ctx", type=int, default=4096)
    ap.add_argument("--ngl", type=int, default=99)
    # 2048, not the 512 first written, and the reason is MEASURED (see the log): Gemma 4 E2B spends
    # a few hundred tokens of thinking channel before any JSON, so at 512 it returns finish=length
    # with EMPTY content on every span that has something to extract - scoring the budget, not the
    # extractor. Llama 3.1 8B finishes an empty answer in 6 tokens and needs none of the headroom.
    # The SAME value is used for both models: a per-model budget would be tuning one of them.
    ap.add_argument("--max-tokens", type=int, default=2048)
    ap.add_argument("--verdict", action="store_true",
                    help="apply the pre-registered rule over every contract-2 run and exit")
    ap.add_argument("--build", default=None, metavar="SUBSTRING",
                    help="with --verdict: only runs whose llama_version contains this, and the "
                         "verdict is written to ms1b_field_verdict_<substring>.json. With --queue: "
                         "every arm asserts its server reports a matching build before it runs.")
    ap.add_argument("--queue", default=None, metavar="k1,k2,...",
                    help="run these model keys sequentially, one server at a time, resumable")
    ap.add_argument("--smoke", default=None, metavar="KEY",
                    help="two real calls against one model; exit 0 iff both parsed")
    ap.add_argument("--log", default=None, help="append one line per queued model here")
    ap.add_argument("--results-dir", default=None)
    ap.add_argument("--offonly", action="store_true",
                    help="with --verdict: the OFF-only reading - only runs that did not think by "
                         "choice - written to ms1b_field_verdict_<build>_offonly.json")
    ap.add_argument("--render", default=None, metavar="KEY",
                    help="render household 1's 167 prompts for KEY on a CPU-only server and write "
                         "its digest; no GPU, no generation")
    ap.add_argument("--render-compare", nargs=2, default=None, metavar=("A.json", "B.json"),
                    help="compare two render digests prompt by prompt")
    a = ap.parse_args(argv)

    seeds_all = list(range(a.seed, a.seed + a.households))

    if a.render_compare:
        left, right = [json.load(open(p, encoding="utf-8")) for p in a.render_compare]
        la, lb = left.get("prompt_sha256") or [], right.get("prompt_sha256") or []
        same = sum(1 for x, y in zip(la, lb) if x == y)
        first = next((i for i, (x, y) in enumerate(zip(la, lb)) if x != y), None)
        print("%s vs %s" % tuple(os.path.basename(p) for p in a.render_compare))
        print("identical %d of %d (lengths %d / %d) | first differing index: %s"
              % (same, min(len(la), len(lb)), len(la), len(lb), first))
        return 0 if (same == len(la) == len(lb)) else 1

    if a.render:
        key = a.render
        tpl_path, tpl_from_sha = resolve_template(key)
        args = resolved_extra_args(key, tpl_path) + ["--device", "none", "--no-warmup", "-t", "2"]
        # CPU only and never the queue's port: a render can run beside nothing, and must be unable
        # to touch a card an arm is using.
        with _client.LlamaServer(model_path(key), port=8099, ctx=a.ctx, ngl=0, extra_args=args,
                                 env={"CUDA_VISIBLE_DEVICES": "-1"}) as srv:
            print("server up: %s  (%s)" % (srv.base_url, srv.version))
            prompts = render_prompts(srv.base_url, key, a.seed, a.days, a.max_tokens)
            build_info = srv.version
            tpl_sha = tpl_from_sha or _props_template_sha(srv.base_url)
        spec = MODELS[key].get("render_expect")
        bad = [i for i, p in enumerate(prompts) if not render_matches(p, spec)]
        digest = render_digest(prompts)
        out = {"key": key, "build_info": build_info, "chat_template_sha256": tpl_sha,
               "think_requested": model_think(key), "think_extra": model_think_extra(key) or None,
               "expect_think": MODELS[key].get("expect_think"), "render_expect": spec,
               "n_prompts": digest["n"], "prompt_sha256": digest["prompt_sha256"],
               "all": digest["all"], "render_ok": not bad}
        path = a.out or os.path.join(a.results_dir or RESULTS_DIR, "render_digest_%s.json" % key)
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(out, fh, indent=2, sort_keys=True)
            fh.write("\n")
        print("render_ok=%s  %d prompts  template %s  spec %r"
              % (not bad, digest["n"], (tpl_sha or "?")[:16], spec))
        if bad:
            print("  %d prompts do not match; first offender #%d ends: %r"
                  % (len(bad), bad[0], prompts[bad[0]][-200:]))
        print("written    : %s" % path)
        return 0 if not bad else 1

    if a.verdict:
        field = load_field(a.results_dir, build=a.build)
        if a.offonly:
            keep = []
            for key, agg, path in field:
                with open(path, encoding="utf-8") as fh:
                    if eligible_offonly(json.load(fh)):
                        keep.append((key, agg, path))
            print("OFF-only reading: %d of %d runs did not think by choice"
                  % (len(keep), len(field)))
            field = keep
        if a.build:
            print("build filter: %r  (%d runs)" % (a.build, len(field)))
        print("%-11s %-9s %-9s %-9s %-9s %-7s %-9s" % ("key", "validity", "F1", "lenientF1",
                                                       "scorable", "zeroGP", "seconds"))
        for key, ag, _path in sorted(field, key=lambda r: -r[1].get("f1", 0.0)):
            print("%-11s %-9.4f %-9.4f %-9.4f %-9.4f %-7d %-9.1f"
                  % (key, ag.get("validity", 0.0), ag.get("f1", 0.0), ag.get("lenient_f1", 0.0),
                     ag.get("f1_scorable", 0.0), ag.get("zero_gold_predictions", 0),
                     ag.get("seconds", 0.0)))
        v = verdict([(k, ag) for k, ag, _ in field])
        line = ("CHOSEN: %s" % v["chosen"]) if v["chosen"] else "NONE"
        print("VERDICT: %s" % line)
        print("reason : %s" % v["reason"])
        op_key, op_reason = (None, "nothing chosen")
        if v["chosen"]:
            op_key, op_reason = operating_key(v["chosen"], [(k, ag) for k, ag, _ in field])
            print("OPERATING: %s" % op_key)
            print("because  : %s" % op_reason)
        out = {"contract": CONTRACT, "schema_sha256": schema_sha256(), "build": a.build,
               "validity_band": VALIDITY_BAND, "f1_floor": F1_FLOOR,
               "n_models": len(field), "chosen": v["chosen"], "reason": v["reason"],
               "offonly": bool(a.offonly),
               "operating_key": op_key, "operating_reason": op_reason,
               "rows": [{"key": k, "validity": ag.get("validity"), "f1": ag.get("f1"),
                         "lenient_f1": ag.get("lenient_f1"),
                         "f1_scorable": ag.get("f1_scorable"),
                         "zero_gold_predictions": ag.get("zero_gold_predictions"),
                         "seconds": ag.get("seconds")}
                        for k, ag, _ in sorted(field, key=lambda r: -r[1].get("f1", 0.0))]}
        vname = "ms1b_field_verdict%s%s.json" % (("_" + a.build) if a.build else "",
                                                 "_offonly" if a.offonly else "")
        vpath = os.path.join(a.results_dir or RESULTS_DIR, vname)
        with open(vpath, "w", encoding="utf-8") as fh:
            json.dump(out, fh, indent=2, sort_keys=True)
            fh.write("\n")
        print("written    : %s" % vpath)
        return 0

    if a.smoke:
        return run_smoke(a.smoke, a.days, a.port, a.ctx, a.ngl, a.max_tokens)

    if a.queue:
        keys = [k.strip() for k in a.queue.split(",") if k.strip()]
        done, skipped, stopped = run_queue(keys, seeds_all, a.days, a.log, a.port, a.ctx, a.ngl,
                                           a.max_tokens, a.results_dir, require_build=a.build)
        _queue_log(a.log, "QUEUE done=%d skipped=%d%s"
                   % (len(done), len(skipped), (" STOPPED at %s" % stopped) if stopped else ""))
        return 1 if stopped else 0

    seeds = seeds_all
    schema = candidate_schema()
    if a.dry_run:
        hh = corpus.generate_household(seeds[0], a.days)
        names, _ = _household_context(hh)
        s = hh["spans"][0]
        body = _client.build_request(s["text"], s["cluster"], s["day"], names, s["sid"], schema,
                                     max_tokens=a.max_tokens)
        print("schema_sha256: %s" % schema_sha256())
        print("spans in household %d: %d ; oracle candidates: %d"
              % (seeds[0], len(hh["spans"]), len(hh["candidates"])))
        print(json.dumps(body, indent=2, sort_keys=True))
        return 0

    res = run_model(a.model, seeds, a.days, a.out, a.port, a.ctx, a.ngl, a.max_tokens)
    ag = res["aggregate"]
    print()
    print("model      : %s (%s)" % (a.model, res["model_path"]))
    print("validity   : %.4f  (%d/%d calls)" % (ag["validity"], ag["valid_calls"],
                                                ag["total_calls"]))
    print("F1         : %.4f   P %.4f  R %.4f   (lenient F1 %.4f)"
          % (ag["f1"], ag["precision"], ag["recall"], ag["lenient_f1"]))
    print("relation   : recall %.4f  (stated %.4f of %d ; inferred %.4f of %d)"
          % (ag["relation_recall"], ag["relation_stated_recall"], ag["relation_stated_gold"],
             ag["relation_inferred_recall"], ag["relation_inferred_gold"]))
    print("preference : polarity agreement %.4f" % ag["preference_polarity_agreement"])
    print("throughput : %d calls in %.1f s ; tokens in %d out %d"
          % (ag["total_calls"], ag["seconds"], ag["tokens_in"], ag["tokens_out"]))
    if a.out:
        print("written    : %s" % a.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
