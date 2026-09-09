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
# Qwen3.5): those run with thinking OFF so every model is measured doing the same job. Gemma 4's
# channel has no switch (the project's JARVIS_THINKING finding) and keeps the headroom instead.
# ORDER here is the queue's fastest-first order, so a winner can emerge before the slow tail.
MODELS = {
    "llama-1b":   {"path": "phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf", "thinking_switch": False},
    "llama-3b":   {"path": "models/Llama-3.2-3B-Instruct-Q4_K_M.gguf",        "thinking_switch": False},
    "phi3-mini":  {"path": "models/Phi-3-mini-4k-instruct-q4.gguf",           "thinking_switch": False},
    "qwen3-4b":   {"path": "models/Qwen3-4B-Q4_K_M.gguf",                     "thinking_switch": True},
    "qwen35-4b":  {"path": "models/Qwen3.5-4B-Q4_K_M.gguf",                   "thinking_switch": True},
    "phi4-mini":  {"path": "models/Phi-4-mini-instruct-Q4_K_M.gguf",          "thinking_switch": False},
    "nuextract":  {"path": "models/NuExtract3-Q4_K_M.gguf",                   "thinking_switch": False},
    "llama-8b":   {"path": "models/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf",   "thinking_switch": False},
    "qwen3-8b":   {"path": "models/Qwen3-8B-Q4_K_M.gguf",                     "thinking_switch": True},
    "qwen35-9b":  {"path": "models/Qwen3.5-9B-Q4_K_M.gguf",                   "thinking_switch": True},
    "gemma-e2b":  {"path": "models/gemma-4-E2B-it-Q4_K_M.gguf",               "thinking_switch": False},
    "gemma-e4b":  {"path": "models/google_gemma-4-E4B-it-Q4_K_M.gguf",        "thinking_switch": False},
}


def model_path(key):
    return MODELS[key]["path"]


def thinking_switch(key):
    return bool(MODELS[key]["thinking_switch"])


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
    mpath = model_path(model_key)
    think = thinking_switch(model_key)
    schema = candidate_schema()
    households, all_results = [], []
    t_start = time.time()
    with _client.LlamaServer(mpath, port=port, ctx=ctx, ngl=ngl) as srv:
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
                                         s["day"], names, schema, max_tokens=max_tokens,
                                         thinking_switch=think)
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
        # REPORTED beside the band, never in it: F1 over the gold a per-span extractor can reach
        # (the inferred edges removed from the recall denominator only), and the predictions on
        # predicates with no gold anywhere in this corpus.
        "scorable_gold": sum(h["scorable_gold"] for h in households),
        "f1_scorable": round(
            (lambda P, R: (2 * P * R / (P + R)) if (P + R) else 0.0)(
                p, tot_match / max(1, sum(h["scorable_gold"] for h in households))), 4),
        "zero_gold_predictions": sum(h["zero_gold_predictions"] for h in households),
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


def load_field(results_dir=None, contract=None):
    """Every CONTRACT-2 run in the results dir, as [(key, aggregate, path)].

    Three exclusions, each for its own reason:
      * `*_contract0.json` / `*_contract1.json` — superseded runs, KEPT on purpose and never mixed
        into a verdict with the contract they were superseded by;
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
        if name.endswith("_contract0.json") or name.endswith("_contract1.json"):
            continue
        with open(path, encoding="utf-8") as fh:
            d = json.load(fh)
        if "model_key" not in d:
            continue
        if d.get("contract") != contract:
            raise ValueError("%s is labelled %r, not %r - a verdict never mixes contracts"
                             % (name, d.get("contract"), contract))
        out.append((d["model_key"], d.get("aggregate", {}), str(path)))
    return out


def _queue_log(log_path, line):
    print(line, flush=True)
    if log_path:
        with open(log_path, "a", encoding="utf-8") as fh:
            fh.write(line + "\n")


def run_queue(keys, seeds, days, log_path, port, ctx, ngl, max_tokens, results_dir=None):
    """Run the field sequentially, ONE server at a time, resumable.

    Idempotent by design because the queue runs for many hours and a session may end under it: a key
    already finished at THIS contract and schema hash is skipped, a key whose JSON was written under
    a different contract STOPS the queue rather than being silently overwritten, and a model file
    that is not on disk is skipped with the reason logged and no JSON created.
    """
    results_dir = results_dir or RESULTS_DIR
    schema_hash = schema_sha256()
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
        _queue_log(log_path, "%s START %s think=%s"
                   % (key, time.strftime("%Y-%m-%d %H:%M:%S"), thinking_switch(key)))
        t0 = time.time()
        try:
            res = run_model(key, seeds, days, out_path, port, ctx, ngl, max_tokens)
        except Exception as exc:                                   # noqa: BLE001
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
    with _client.LlamaServer(model_path(key), port=port, ctx=ctx, ngl=ngl) as srv:
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
    ap.add_argument("--queue", default=None, metavar="k1,k2,...",
                    help="run these model keys sequentially, one server at a time, resumable")
    ap.add_argument("--smoke", default=None, metavar="KEY",
                    help="two real calls against one model; exit 0 iff both parsed")
    ap.add_argument("--log", default=None, help="append one line per queued model here")
    ap.add_argument("--results-dir", default=None)
    a = ap.parse_args(argv)

    seeds_all = list(range(a.seed, a.seed + a.households))

    if a.verdict:
        field = load_field(a.results_dir)
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
        out = {"contract": CONTRACT, "schema_sha256": schema_sha256(),
               "validity_band": VALIDITY_BAND, "f1_floor": F1_FLOOR,
               "n_models": len(field), "chosen": v["chosen"], "reason": v["reason"],
               "rows": [{"key": k, "validity": ag.get("validity"), "f1": ag.get("f1"),
                         "lenient_f1": ag.get("lenient_f1"),
                         "f1_scorable": ag.get("f1_scorable"),
                         "zero_gold_predictions": ag.get("zero_gold_predictions"),
                         "seconds": ag.get("seconds")}
                        for k, ag, _ in sorted(field, key=lambda r: -r[1].get("f1", 0.0))]}
        vpath = os.path.join(a.results_dir or RESULTS_DIR, "ms1b_field_verdict.json")
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
                                           a.max_tokens, a.results_dir)
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
