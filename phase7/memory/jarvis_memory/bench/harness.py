"""Replay a seeded household day by day into a real MemoryStore, then score the five §8 sets.

The replay is day by day rather than all at once for one reason: the guess. The owner-to-partner
spouse edge is supposed to CLIMB - a confidence that accrues over distinct evidence days - so the
only honest way to report the day it crosses the surfacing threshold is to ask the store after each
day, exactly as the console would.

Two mechanics are worth knowing before reading a number out of this file:

  * **Candidates wait for their people.** An edge needs both people to exist, and a cluster becomes
    a person only on its third full day. A candidate whose person is not there yet is held and
    re-offered every day until it lands, carrying its ORIGINAL spans - which is the design's amended
    §3.4: personhood gates the person row, never the evidence, so the pre-personhood days still
    count once the edge is first computed.
  * **The oracle supplies the partner's display name at promotion.** Earning a name from vocatives
    is MS2's mechanism (design §5); at MS0 the harness sets it, which is what "oracle candidates"
    means. It is a benchmark affordance and is named here rather than hidden.

Nothing here is a model. Standard library only.
"""
import json
import statistics
import time

from ..confidence import SURFACE_THRESHOLD
from ..store import MemoryStore
from . import corpus as _corpus


def _resolve(ref, ids):
    """A corpus ref ('owner' / 'partner' / None) to a person id, or None if not yet a person."""
    if ref is None:
        return None
    return ids.get(ref)


def _prepare(cand, ids, sid_map):
    """Turn a corpus candidate into the store's shape, or None if its people do not exist yet."""
    out = dict(cand)
    out.pop("day", None)
    subj = dict(cand["subject"])
    ref = subj.pop("ref", None)
    if subj.get("kind") == "person":
        pid = _resolve(ref, ids)
        if pid is None:
            return None
        subj["id"] = pid
    else:
        subj["id"] = None
    out["subject"] = subj
    if cand["predicate_id"] == "person.relation_to":
        target = _resolve(cand["object"], ids)
        if target is None:
            return None
        out["object"] = target
    out["span_ids"] = [sid_map[s] for s in cand["span_ids"] if s in sid_map]
    if cand.get("contradicts"):
        out["contradicts"] = [sid_map[s] for s in cand["contradicts"] if s in sid_map]
    if not out["span_ids"]:
        return None
    return out


def _spouse_confidence(st, owner_id, partner_id):
    if owner_id is None or partner_id is None:
        return None
    rows = st.current("edge", from_person=owner_id, to_person=partner_id, relation_id="spouse")
    return rows[0]["confidence"] if rows else None


def _fact_row(st, row_id):
    r = st.conn.execute(
        "select subject_id, object_norm, predicate_id from fact where id=?", (row_id,)).fetchone()
    return dict(r) if r else None


def _score_update(st, items, ids, now, hint="auto", embedder=None):
    ok = 0
    for it in items:
        hits = st.query(it["query"], k=5, now=now, predicate_hint=hint, embedder=embedder)
        if not hits or hits[0]["table"] != "fact":
            continue
        row = _fact_row(st, hits[0]["row_id"])
        if not row:
            continue
        want_subject = _resolve(it["subject"], ids)
        if row["subject_id"] == want_subject and row["object_norm"] == it["gold_object_norm"]:
            ok += 1
    return ok / len(items) if items else 0.0


def _score_coexist(st, items, ids, now, hint="auto", embedder=None):
    """Recall of the current values, with a hard zero if a value the owner ENDED comes back."""
    recalls, leaks = [], 0
    for it in items:
        hits = st.query(it["query"], k=5, now=now, predicate_hint=hint, embedder=embedder)
        found = set()
        leaked = False
        for h in hits:
            if h["table"] != "fact":
                continue
            row = _fact_row(st, h["row_id"])
            if not row or row["predicate_id"] != it["predicate_id"]:
                continue
            if row["object_norm"] in it["gold_object_norms"]:
                found.add(row["object_norm"])
            if it["ended_object_norm"] and row["object_norm"] == it["ended_object_norm"]:
                leaked = True
        if leaked:
            leaks += 1
            recalls.append(0.0)
        else:
            recalls.append(len(found) / len(it["gold_object_norms"]))
    return (statistics.fmean(recalls) if recalls else 0.0), leaks


def pref_in_top5_rate(st, queries, now, hint="auto", embedder=None) -> float:
    """The MEASURED PRICE of the MS1a.3 preference lane: the fraction of FACT questions whose top
    five contains at least one preference row.

    The lane gives the preference model its own rank restart, so on any question at least one
    preference is a candidate. That is the cost of the transfer gain and it is reported, never
    assumed away - and it is a rate, not a claim that the preference outranks the answer (T30d pins
    that it does not).
    """
    if not queries:
        return 0.0
    n = 0
    for q in queries:
        hits = st.query(q, k=5, now=now, predicate_hint=hint, embedder=embedder)
        if any(h["table"] == "preference" for h in hits):
            n += 1
    return n / len(queries)


def _gold_pref_rank(st, query, topic, embedder):
    """1-based cosine rank of the planted preference among the household's CURRENT preferences.

    The diagnostic beside the transfer band (design §6, MS1a.2): rank 1 and still missed means the
    FUSION is what loses it; rank > 1 means the EMBEDDER never had it. Uses the store's own vector
    lane, so it measures what retrieval sees rather than a second embedding path, and the limit is
    large enough that no preference is cut off. None with no embedder.
    """
    if embedder is None:
        return None
    keys = [k for k, _ in st._vector_lane(query, embedder, limit=100000) if k[0] == "preference"]
    for i, key in enumerate(keys, start=1):
        r = st.conn.execute("select topic_norm from preference where id=?", (key[1],)).fetchone()
        if r and r[0] == topic:
            return i
    return None


def _gold_pref_lane_rank(st, query, topic, embedder):
    """The planted preference's 1-based rank INSIDE the MS1a.3 preference lane (`vec_pref`).

    `_gold_pref_rank` measures the mixed, symmetric lane; this one measures the lane that actually
    orders preferences at query time - preference rows only, instruction-prefixed query - so the
    diagnostics can still see the mechanism after MS1a.3 added it (the coder's F7). None with no
    embedder.
    """
    if embedder is None:
        return None
    keys = [k for k, _ in st._vector_lane(query, embedder, limit=100000,
                                          tables=("preference",), instruction=True)]
    for i, key in enumerate(keys, start=1):
        r = st.conn.execute("select topic_norm from preference where id=?", (key[1],)).fetchone()
        if r and r[0] == topic:
            return i
    return None


def _gold_vec_rank(st, query, topic, embedder):
    """The same preference's 1-based rank in the WHOLE vector lane, spans and facts included.

    `_gold_pref_rank` says whether the embedder can pick the right preference out of the other
    preferences; this says how much else the query pulls in ahead of it, which is what the
    crowding-out story of MS1a §3.9 is really about. None with no embedder.
    """
    if embedder is None:
        return None
    for i, (key, _cos) in enumerate(st._vector_lane(query, embedder, limit=100000), start=1):
        if key[0] != "preference":
            continue
        r = st.conn.execute("select topic_norm from preference where id=?", (key[1],)).fetchone()
        if r and r[0] == topic:
            return i
    return None


def _score_transfer(st, items, owner_id, now, hint="auto", embedder=None):
    """Is the planted preference in the top five for a scenario worded without its own words?

    At MS0 the full-text lane is the only lane and preferences are not in it, so this is expected to
    be 0 and is REPORTED, never banded - the design's §8 moves the band to MS1 with the embedding
    lane for exactly this reason.
    """
    ok = 0
    by_topic = {}
    ranks = []
    for it in items:
        topic = it["gold_topic_norm"]
        by_topic.setdefault(topic, 0)
        ranks.append({"query": it["query"], "topic": topic,
                      "pref_rank": _gold_pref_rank(st, it["query"], topic, embedder),
                      "vec_rank": _gold_vec_rank(st, it["query"], topic, embedder),
                      "pref_lane_rank": _gold_pref_lane_rank(st, it["query"], topic, embedder)})
        hits = st.query(it["query"], k=5, now=now, predicate_hint=hint, embedder=embedder)
        for h in hits:
            if h["table"] != "preference":
                continue
            r = st.conn.execute("select topic_norm from preference where id=?",
                                (h["row_id"],)).fetchone()
            if r and r[0] == topic:
                ok += 1
                by_topic[topic] += 1
                break
    return (ok / len(items) if items else 0.0), by_topic, ranks


def _score_relations(st, items, ids):
    gold = {(_resolve(i["from"], ids), _resolve(i["to"], ids), i["relation_id"]) for i in items}
    surfaced = st.conn.execute(
        "select from_person, to_person, relation_id from edge "
        "where valid_to is null and confidence >= ?", (SURFACE_THRESHOLD,)).fetchall()
    if not surfaced:
        return 0.0, 0
    hit = sum(1 for r in surfaced if (r[0], r[1], r[2]) in gold)
    return hit / len(surfaced), len(surfaced)


def run_household(seed, days, predicate_hint=True, embedder=None, drop_stopwords=True):
    """`predicate_hint=False` is the NEGATIVE CONTROL: the MS0 lane, unrestricted.

    `embedder` adds the vector lane. It is applied by `embed_pending` AFTER ingest, never during:
    the write path stays embedding-free so the p99 write band measures the store, not a GPU.
    """
    hint = "auto" if predicate_hint else None
    hh = _corpus.generate_household(seed, days)
    st = MemoryStore(":memory:", drop_stopwords=drop_stopwords)
    clusters = {c: st.add_cluster() for c in hh["clusters"]}
    owner_name = hh["persons"][0]["name"]
    partner_name = hh["persons"][1]["name"]
    ids = {"owner": st.bind_owner(clusters[1], owner_name)}

    spans_by_day = {}
    for sp in hh["spans"]:
        spans_by_day.setdefault(sp["day"], []).append(sp)
    cands_by_day = {}
    for c in hh["candidates"]:
        cands_by_day.setdefault(c["day"], []).append(c)

    sid_map, pending = {}, []
    spouse_day = None
    for day in range(1, days + 1):
        by_cluster = {}
        for sp in spans_by_day.get(day, []):
            by_cluster.setdefault(sp["cluster"], []).append(sp)
        for cl, sps in sorted(by_cluster.items()):
            rec = st.add_recording(f"sha-{seed}-{day}-{cl}", f"{sps[0]['said_at'][:10]}T00:00:00",
                                   86400.0, "headset")
            for sp in sps:
                secs = (int(sp["said_at"][11:13]) * 3600 + int(sp["said_at"][14:16]) * 60
                        + int(sp["said_at"][17:19]))
                sid_map[sp["sid"]] = st.add_span(rec, float(secs), float(secs) + 4.0,
                                                 clusters[cl], sp["text"], 0.95)
        for pid in st.promote_persons():
            # The oracle stands in for MS2's name-earning; see the module docstring.
            if "partner" not in ids:
                ids["partner"] = pid
                st.conn.execute("update person set display_name=?, name_confidence=1.0, "
                                "name_source_kind='stated_owner' where id=?", (partner_name, pid))
                st.conn.commit()

        queue, pending = pending + cands_by_day.get(day, []), []
        for c in queue:
            prepared = _prepare(c, ids, sid_map)
            if prepared is None:
                pending.append(c)
                continue
            st.ingest(prepared)

        conf = _spouse_confidence(st, ids.get("owner"), ids.get("partner"))
        if spouse_day is None and conf is not None and conf >= SURFACE_THRESHOLD:
            spouse_day = day

    embed_stats = {"fact": 0, "preference": 0, "span": 0, "seconds": 0.0}
    if embedder is not None:
        embed_stats = st.embed_pending(embedder)

    now = _corpus._said_at(days, 86000)
    update_acc = _score_update(st, hh["sets"]["update"], ids, now, hint, embedder)
    update_para = _score_update(st, hh["sets"]["update_paraphrase"], ids, now, hint, embedder)
    coexist_recall, ended_leaks = _score_coexist(st, hh["sets"]["coexist"], ids, now, hint, embedder)
    transfer, by_topic, gold_ranks = _score_transfer(st, hh["sets"]["transfer"], ids["owner"],
                                                     now, hint, embedder)
    # The MS1a.3 preference lane's measured PRICE, over the same UPDATE questions the band scores:
    # how often a preference occupies one of a fact question's five results. REPORTED, never banded.
    pref_price = pref_in_top5_rate(st, [it["query"] for it in hh["sets"]["update"]],
                                   now, hint, embedder)
    rel_prec, n_surfaced = _score_relations(st, hh["sets"]["relations"], ids)
    spouse_conf = _spouse_confidence(st, ids.get("owner"), ids.get("partner"))

    # --- growth: 30x unrelated transcript into the SAME store, then re-ask ---
    filler_rec = {}
    for f in hh["sets"]["growth_filler"]:
        day = f["day"]
        if day not in filler_rec:
            filler_rec[day] = st.add_recording(f"sha-filler-{seed}-{day}",
                                               f["said_at"][:10] + "T00:00:00", 86400.0, "filler")
        pid = st.conn.execute("select id from person where display_name=?",
                              (f["person_ref"],)).fetchone()
        if pid is None:
            cur = st.conn.execute(
                "insert into person (kind, display_name, created_at) values ('cluster',?,?)",
                (f["person_ref"], f["said_at"]))
            pid = (cur.lastrowid,)
        sid = st.add_span(filler_rec[day], 700.0, 704.0, clusters[1], f["span_text"], 0.95)
        st.ingest({"predicate_id": f["predicate_id"],
                   "subject": {"kind": "person", "id": pid[0]},
                   "object": f["object"], "object_norm": f["object_norm"],
                   "source_kind": "stated_owner", "speaker_cluster": clusters[1],
                   "span_ids": [sid], "about_time": None, "relation_id": None,
                   "polarity": None, "strength": None, "ended": False, "said_at": f["said_at"]})
    if embedder is not None:
        more = st.embed_pending(embedder)
        for kk in ("fact", "preference", "span"):
            embed_stats[kk] += more[kk]
        embed_stats["seconds"] = round(embed_stats["seconds"] + more["seconds"], 3)
    growth_acc = _score_update(st, hh["sets"]["update"], ids, now, hint, embedder)
    growth_para = _score_update(st, hh["sets"]["update_paraphrase"], ids, now, hint, embedder)

    violations = len(st.audit_violations())
    n_facts = st.conn.execute("select count(*) from fact").fetchone()[0]
    st.close()
    return {
        "seed": seed,
        "n_candidates": len(hh["candidates"]),
        "n_filler": len(hh["sets"]["growth_filler"]),
        "n_facts_after_growth": n_facts,
        "update_acc": round(update_acc, 4),
        "update_acc_paraphrase": round(update_para, 4),
        "transfer_by_topic": by_topic,
        "pref_in_top5_rate": round(pref_price, 4),
        # REPORTED, never a band: how the planted preference ranked, so a transfer miss can be
        # attributed to the fusion (rank 1, still missed) or to the embedder (rank > 1).
        "transfer_gold_ranks": gold_ranks,
        "transfer_gold_pref_rank1": round(
            (sum(1 for r in gold_ranks if r["pref_rank"] == 1) / len(gold_ranks))
            if gold_ranks else 0.0, 4),
        "transfer_gold_pref_rank_mean": (
            round(statistics.fmean([r["pref_rank"] for r in gold_ranks
                                    if r["pref_rank"] is not None]), 4)
            if any(r["pref_rank"] is not None for r in gold_ranks) else None),
        "transfer_gold_vec_rank_mean": (
            round(statistics.fmean([r["vec_rank"] for r in gold_ranks
                                    if r["vec_rank"] is not None]), 4)
            if any(r["vec_rank"] is not None for r in gold_ranks) else None),
        "transfer_gold_pref_lane_rank_mean": (
            round(statistics.fmean([r["pref_lane_rank"] for r in gold_ranks
                                    if r["pref_lane_rank"] is not None]), 4)
            if any(r["pref_lane_rank"] is not None for r in gold_ranks) else None),
        "embed_seconds": embed_stats["seconds"],
        "n_embedded": embed_stats["fact"] + embed_stats["preference"] + embed_stats["span"],
        "coexist_recall": round(coexist_recall, 4),
        "coexist_ended_leaks": ended_leaks,
        "transfer_recall5": round(transfer, 4),
        "relation_precision": round(rel_prec, 4),
        "relations_surfaced": n_surfaced,
        "spouse_surfaced_day": spouse_day,
        "spouse_confidence_last_day": round(spouse_conf, 4) if spouse_conf is not None else None,
        "growth_update_acc": round(growth_acc, 4),
        "growth_drop_points": round(100.0 * (update_acc - growth_acc), 4),
        "growth_update_acc_paraphrase": round(growth_para, 4),
        "growth_drop_paraphrase_points": round(100.0 * (update_para - growth_para), 4),
        "audit_violations": violations,
    }


def measure_latency(n_facts, n_subjects=2000):
    """Per-ingest wall time on single-valued candidates over a fixed subject pool, so supersedes
    happen. A fresh in-memory store: the number is the store's write path, not the disk's."""
    if not n_facts:
        return None
    st = MemoryStore(":memory:")
    cl = st.add_cluster()
    rec = st.add_recording("sha-latency", "2026-03-01T00:00:00", 86400.0, "bench")
    people = [st.conn.execute("insert into person (kind, display_name, created_at) "
                              "values ('cluster',?, '2026-03-01T00:00:00')",
                              (f"p{i}",)).lastrowid for i in range(n_subjects)]
    st.conn.commit()
    spans = [st.add_span(rec, float(i), float(i) + 2.0, cl, f"utterance {i}", 0.9)
             for i in range(min(n_subjects, 500))]
    times = []
    for i in range(n_facts):
        cand = {"predicate_id": "person.lives_in",
                "subject": {"kind": "person", "id": people[i % n_subjects]},
                "object": f"town{i}", "object_norm": f"town{i}",
                "source_kind": "stated_owner", "speaker_cluster": cl,
                "span_ids": [spans[i % len(spans)]], "about_time": None, "relation_id": None,
                "polarity": None, "strength": None, "ended": False,
                "said_at": f"2026-03-01T00:00:{i % 60:02d}"}
        t0 = time.perf_counter()
        st.ingest(cand)
        times.append((time.perf_counter() - t0) * 1000.0)
    times.sort()
    p50 = times[len(times) // 2]
    p99 = times[min(len(times) - 1, int(len(times) * 0.99))]
    total = st.conn.execute("select count(*) from fact").fetchone()[0]
    st.close()
    return {"n_facts": total, "p50_ms": round(p50, 4), "p99_ms": round(p99, 4),
            "n_ingests": n_facts}


def run(seeds, days, latency_facts, out_path=None, predicate_hint=True, embedder=None,
        embedder_name="none", drop_stopwords=True) -> dict:
    households = [run_household(s, days, predicate_hint, embedder, drop_stopwords)
                  for s in seeds]
    agg = {}
    for field in ("update_acc", "coexist_recall", "transfer_recall5", "relation_precision",
                  "growth_update_acc", "growth_drop_points", "update_acc_paraphrase",
                  "growth_update_acc_paraphrase", "growth_drop_paraphrase_points"):
        agg[field] = round(statistics.fmean(h[field] for h in households), 4)
    # REPORTED, never banded, and None-safe: with no embedder every rank is None, so the mean is
    # None rather than a fabricated 0 - the rank-1 FRACTION is 0.0 there by its own definition.
    for field in ("transfer_gold_pref_rank1", "pref_in_top5_rate"):
        agg[field] = round(statistics.fmean(h[field] for h in households), 4)
    for field in ("transfer_gold_pref_rank_mean", "transfer_gold_vec_rank_mean",
                  "transfer_gold_pref_lane_rank_mean"):
        vals = [h[field] for h in households if h[field] is not None]
        agg[field] = round(statistics.fmean(vals), 4) if vals else None

    surfaced_days = [h["spouse_surfaced_day"] for h in households
                     if h["spouse_surfaced_day"] is not None]
    agg["spouse_surfaced_day_mean"] = (round(statistics.fmean(surfaced_days), 4)
                                       if surfaced_days else None)
    agg["spouse_surfaced_households"] = f"{len(surfaced_days)}/{len(households)}"
    latency = measure_latency(latency_facts)
    violations = sum(h["audit_violations"] for h in households)

    bands = {
        "update_acc>=0.95": agg["update_acc"] >= 0.95,
        "coexist_recall>=0.95": agg["coexist_recall"] >= 0.95,
        "audit==0": violations == 0,
        "growth_drop<=5": agg["growth_drop_points"] <= 5.0,
        "p99<=50ms": (latency["p99_ms"] <= 50.0) if latency else None,
    }
    # The transfer band applies only when a vector lane exists: the full-text lane cannot match a
    # scenario that shares no word with its preference, which is why MS0/MS0.1 reported it instead.
    if embedder_name != "none":
        bands["transfer_recall5>=0.60"] = agg["transfer_recall5"] >= 0.60
    topics = {}
    for h in households:
        for t, n in (h.get("transfer_by_topic") or {}).items():
            topics[t] = topics.get(t, 0) + n
    out = {
        "predicate_hint": bool(predicate_hint),
        "stopwords": "dropped" if drop_stopwords else "kept",
        "embedder": embedder_name,
        "embedder_version": getattr(embedder, "version", None),
        "embedder_load_s": getattr(embedder, "load_s", None),
        "query_instruction": bool(getattr(embedder, "instruction", False)),
        "transfer_by_topic_total": topics,
        "households": households,
        "aggregate": agg,
        "latency": latency,
        "audit_violations": violations,
        "bands": bands,
        "reported": {
            "transfer_recall5": agg["transfer_recall5"],
            "transfer_gold_pref_rank1": agg["transfer_gold_pref_rank1"],
            "transfer_gold_vec_rank_mean": agg["transfer_gold_vec_rank_mean"],
            "transfer_gold_pref_lane_rank_mean": agg["transfer_gold_pref_lane_rank_mean"],
            "pref_in_top5_rate": agg["pref_in_top5_rate"],
            "relation_precision": agg["relation_precision"],
            "spouse_surfaced_day_mean": agg["spouse_surfaced_day_mean"],
        },
        "scope": ("MS0: the full-text lane only, ORACLE candidates, a seeded template corpus. "
                  "Transfer and relation precision are REPORTED, not banded - the embedding lane "
                  "lands at MS1. Nothing here is measured on real speech or on the owner."),
    }
    if out_path:
        with open(out_path, "w", encoding="utf-8") as fh:
            json.dump(out, fh, indent=2, sort_keys=True)
            fh.write("\n")
    return out
