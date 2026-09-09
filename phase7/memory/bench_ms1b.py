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
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from jarvis_memory.bench import corpus                      # noqa: E402
from jarvis_memory.extract import client as _client         # noqa: E402
from jarvis_memory.extract.derive import derive             # noqa: E402
from jarvis_memory.extract.schema import candidate_schema, schema_sha256   # noqa: E402
from jarvis_memory.extract.score import score_household, validity          # noqa: E402

# The contract the model was held to, recorded in every run's JSON beside the schema hash. "wide"
# was the first shape (the model restating subject, speaker, source kind, object_norm and span ids);
# "narrow" is the decision set. Two runs are comparable only if this and the schema hash agree —
# run L0 is a "wide" run kept as the measurement of the contract, never as the band.
CONTRACT = "narrow"

MODELS = {
    "gemma-e2b": "models/gemma-4-E2B-it-Q4_K_M.gguf",
    "llama-8b": "models/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf",
}


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


def _household_context(hh):
    """The names the contract allows the extractor to see: the owner's and the partner's."""
    names_by_cluster, clusters_by_name = {}, {}
    for i, p in enumerate(hh["persons"][:2], start=1):
        names_by_cluster[i] = p["name"]
        clusters_by_name[str(p["name"]).lower()] = i
    return names_by_cluster, clusters_by_name


def run_model(model_key, seeds, days, out_path, port, ctx, ngl, max_tokens):
    model_path = MODELS[model_key]
    schema = candidate_schema()
    households, all_results = [], []
    t_start = time.time()
    with _client.LlamaServer(model_path, port=port, ctx=ctx, ngl=ngl) as srv:
        print("server up: %s  (%s)" % (srv.base_url, srv.version))
        for seed in seeds:
            hh = corpus.generate_household(seed, days)
            names, clusters_by_name = _household_context(hh)
            span_cluster = {s["sid"]: s["cluster"] for s in hh["spans"]}
            span_text = {s["sid"]: s["text"] for s in hh["spans"]}
            preds, results = [], []
            t0 = time.time()
            for s in hh["spans"]:
                r = _client.extract_span(srv.base_url, s["sid"], s["text"], s["cluster"],
                                         s["day"], names, schema, max_tokens=max_tokens)
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
        "model_path": model_path,
        "model_bytes": os.path.getsize(model_path),
        "model_sha256": _sha256(model_path),
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
    ap.add_argument("--verdict", nargs="*", default=None, metavar="RUN_JSON",
                    help="apply the pre-registered rule to finished runs and exit")
    a = ap.parse_args(argv)

    if a.verdict is not None:
        runs = []
        for p in a.verdict:
            with open(p, encoding="utf-8") as fh:
                d = json.load(fh)
            runs.append((d.get("model_key", p), d.get("aggregate", {})))
            print("%-10s contract=%-6s validity %.4f  F1 %.4f  (schema %s)"
                  % (d.get("model_key", p), d.get("contract", "?"),
                     d["aggregate"]["validity"], d["aggregate"]["f1"],
                     str(d.get("schema_sha256"))[:12]))
        v = verdict(runs)
        print("VERDICT: %s" % ("CHOSEN: %s" % v["chosen"] if v["chosen"] else "NONE"))
        print("reason : %s" % v["reason"])
        return 0

    seeds = list(range(a.seed, a.seed + a.households))
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
