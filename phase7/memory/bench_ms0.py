#!/usr/bin/env python3
"""Run the MS0 household benchmark and print the aggregate and the pre-registered bands.

    python3 phase7/memory/bench_ms0.py --households 10 --days 14 --seed 1 \
        --latency-facts 100000 --out phase7/memory/bench/results/ms0_run.json --assert-bands

Seeds are `seed .. seed+households-1`. `--assert-bands` exits 1 if any band is False; a band that is
None (the latency band when --latency-facts is 0) is not a failure, it is a measurement that was not
taken. The bands come from the design's §8 and are fixed before the code that measures them; a miss
is a finding with its cause named, never a knob turned.

Standard library only, no model, no GPU, nothing about the owner: the corpus is a seeded template.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from jarvis_memory.bench import harness  # noqa: E402


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--households", type=int, default=10)
    p.add_argument("--days", type=int, default=14)
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--latency-facts", type=int, default=0)
    p.add_argument("--out", default=None)
    p.add_argument("--assert-bands", action="store_true")
    p.add_argument("--no-predicate-hint", dest="predicate_hint", action="store_false",
                   help="the NEGATIVE CONTROL: run the MS0 lane, unrestricted by the registry hint")
    p.add_argument("--embedder", choices=("none", "qwen"), default="none",
                   help="none keeps the full-text lanes only; qwen adds the vector lane on the GPU")
    p.add_argument("--query-instruction", action="store_true",
                   help="the REPORTED arm: prefix the query with the instruction, never adopted")
    a = p.parse_args(argv)

    seeds = list(range(a.seed, a.seed + a.households))
    embedder = None
    if a.embedder == "qwen":
        from jarvis_memory.embed import QwenEmbedder   # imports torch; only on this path
        embedder = QwenEmbedder(instruction=a.query_instruction)
        print(f"embedder   : {embedder.model_id} dim {embedder.dim} "
              f"loaded in {embedder.load_s} s on {embedder.device} "
              f"(sentence-transformers {embedder.version})")
    res = harness.run(seeds, a.days, a.latency_facts, a.out, a.predicate_hint, embedder,
                      a.embedder)

    print(f"households : {len(seeds)}  seeds {seeds[0]}..{seeds[-1]}  days {a.days}  "
          f"predicate_hint {'ON' if a.predicate_hint else 'OFF (negative control)'}  "
          f"embedder {a.embedder}{' +instruct' if a.query_instruction else ''}")
    print("aggregate  :")
    for k in sorted(res["aggregate"]):
        print(f"    {k:32s} {res['aggregate'][k]}")
    if res["latency"]:
        lat = res["latency"]
        print(f"latency    : p50 {lat['p50_ms']} ms  p99 {lat['p99_ms']} ms "
              f"over {lat['n_ingests']} ingests, {lat['n_facts']} facts in the store")
    else:
        print("latency    : not measured (--latency-facts 0)")
    print(f"audit      : {res['audit_violations']} violations")
    print("bands      :")
    failed = []
    for k in sorted(res["bands"]):
        v = res["bands"][k]
        mark = "PASS" if v is True else ("n/a " if v is None else "FAIL")
        print(f"    {mark} {k}")
        if v is False:
            failed.append(k)
    print("reported   : " + json.dumps(res["reported"], sort_keys=True))
    if res.get("transfer_by_topic_total"):
        print("transfer/topic: " + json.dumps(res["transfer_by_topic_total"], sort_keys=True)
              + f"  (of {3 * len(seeds)} scenarios each)")
    if a.out:
        print(f"written    : {a.out}")
    if a.assert_bands and failed:
        print(f"\nBANDS MISSED: {', '.join(failed)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
