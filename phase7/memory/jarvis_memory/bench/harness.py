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

from .. import people as _people
from ..confidence import SURFACE_THRESHOLD
from ..registry import EDGE_PREDICATE as _EDGE_PREDICATE
from ..store import MemoryStore
from . import corpus as _corpus


def _resolve(ref, ids, alias=None):
    """A ref to a person id, or None if that person does not exist in the store yet.

    The ORACLE says "owner" / "partner" / None and is unchanged by the alias argument: with
    `alias=None` this is `ids.get(ref)` exactly as before. An EXTRACTED candidate (MS1b's winner run)
    says what the extractor said - a cluster id as a string, or a name - so the caller supplies the
    map from those to the same two keys. Returning None keeps the existing semantics: the candidate
    is held pending until the person exists.
    """
    if ref is None:
        return None
    key = str(ref).strip().lower()
    if alias:
        key = alias.get(key, key)
    return ids.get(key)


def _prepare(cand, ids, sid_map, alias=None):
    """Turn a candidate into the store's shape, or None if its people do not exist yet."""
    out = dict(cand)
    out.pop("day", None)
    subj = dict(cand["subject"])
    ref = subj.pop("ref", None)
    if subj.get("kind") == "person":
        pid = _resolve(ref, ids, alias)
        if pid is None:
            return None
        subj["id"] = pid
    else:
        subj["id"] = None
    out["subject"] = subj
    if cand["predicate_id"] == "person.relation_to":
        target = _resolve(cand["object"], ids, alias)
        if target is None:
            return None
        out["object"] = target
    out["span_ids"] = [sid_map[s] for s in cand["span_ids"] if s in sid_map]
    if cand.get("contradicts"):
        out["contradicts"] = [sid_map[s] for s in cand["contradicts"] if s in sid_map]
    if not out["span_ids"]:
        return None
    return out


def _edge_confidence(st, owner_id, partner_id, relation_id):
    """The current owner->partner edge's confidence for one relation, or None if there is no row."""
    if owner_id is None or partner_id is None:
        return None
    rows = st.current("edge", from_person=owner_id, to_person=partner_id,
                      relation_id=relation_id)
    return rows[0]["confidence"] if rows else None


def _spouse_confidence(st, owner_id, partner_id):
    return _edge_confidence(st, owner_id, partner_id, "spouse")


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


def _surfaced_edges(st) -> list:
    """Every CURRENT edge at or above the surfacing threshold, as plain dicts."""
    return [dict(r) for r in st.conn.execute(
        "select from_person, to_person, relation_id, confidence, source_kind from edge "
        "where valid_to is null and confidence >= ?", (SURFACE_THRESHOLD,)).fetchall()]


def _score_relations(st, items, ids):
    """Both relation figures over the same surfaced set, so they can never disagree.

    `relation_precision` is the HISTORICAL per-edge figure and keeps its name and its computation:
    every surfaced edge judged against the planted triples. With the people layer on the oracle path
    it moves from 1.0 to 0.6667 BY CONSTRUCTION - the rules add a `partner` edge beside the oracle's
    `spouse` and each household surfaces three edges where it surfaced two - which is pre-registered
    in the design, not a control that moved.

    `relation_precision_pairs` is the MS2 band's figure: the finest surfaced edge per ordered pair,
    so the layer is not penalised for being more specific about one relationship. The two are
    different questions and the report must never print one as the other.
    """
    gold_triples = {(_resolve(i["from"], ids), _resolve(i["to"], ids), i["relation_id"])
                    for i in items}
    gold_by_pair = {(_resolve(i["from"], ids), _resolve(i["to"], ids)): i["relation_id"]
                    for i in items}
    surfaced = _surfaced_edges(st)
    sc = _people.score_pairs(surfaced, gold_by_pair)
    hit = sum(1 for r in surfaced
              if (r["from_person"], r["to_person"], r["relation_id"]) in gold_triples)
    return {
        "relation_precision": (hit / len(surfaced)) if surfaced else 0.0,
        "relations_surfaced": len(surfaced),
        "relations_surfaced_pairs": sc["pairs"],
        "relations_fine": sc["fine"],
        "relations_coarse": sc["coarse"],
        "relations_wrong": sc["wrong"],
        # POOLED at the aggregate; None here when this household surfaced no pair at all, so an
        # absent measurement is never averaged in as a zero.
        "relation_precision_pairs": (((sc["fine"] + sc["coarse"]) / sc["pairs"])
                                     if sc["pairs"] else None),
    }


def ms2_bands(agg, n_households, violations, latency, embedder_name) -> dict:
    """The design's §8 extracted column plus the cross-cutting bands, as amended in §11.

    Pure, and the ONLY place a band threshold is written: every other module reads a measurement,
    so a threshold can never be moved by editing the thing that produces the number. A band that is
    None was not measured (no embedder, no latency run) and is not a failure - the distinction the
    MS0 harness already draws, kept.
    """
    pairs = agg.get("relation_precision_pairs")
    return {
        "update_acc>=0.85": agg["update_acc"] >= 0.85,
        "coexist_recall>=0.85": agg["coexist_recall"] >= 0.85,
        "transfer_recall5>=0.60": ((agg["transfer_recall5"] >= 0.60)
                                   if embedder_name != "none" else None),
        "growth_drop<=5": agg["growth_drop_points"] <= 5.0,
        "relationship_surfaced>=0.8": (agg["relationship_surfaced_count"]
                                       >= 0.8 * n_households),
        # None is a MISS, not an absence: no pair surfaced means the layer produced nothing to be
        # precise about, and scoring that as "not measured" would hide the failure it is.
        "relation_precision_pairs>=0.90": (pairs >= 0.90) if pairs is not None else False,
        "relations_wrong==0": agg["relations_wrong"] == 0,
        "audit==0": violations == 0,
        "p99<=50ms": (latency["p99_ms"] <= 50.0) if latency else None,
    }


def _env_block(embedder=None) -> dict:
    """What produced these numbers, recorded in every output file.

    The GPU name and driver are read on EVERY run because the lane-ON control's disposition (design
    §8) turns on whether two runs shared an environment; the library versions are read only when an
    embedder is given, so the stdlib-only path never imports torch and the CI step is unaffected.
    Any failure is RECORDED as a string and never raised: provenance must not be able to kill a run.
    """
    import platform
    import subprocess
    env = {"python": platform.python_version()}
    try:
        out = subprocess.run(["nvidia-smi", "--query-gpu=name,driver_version",
                              "--format=csv,noheader"],
                             capture_output=True, text=True, timeout=30)
        env["gpu"] = (out.stdout or out.stderr or "").strip().splitlines()[0].strip()
    except Exception as exc:                                       # noqa: BLE001 - recorded
        env["gpu"] = "unavailable: %r" % (exc,)
    if embedder is not None:
        for name, mod in (("torch", "torch"), ("transformers", "transformers"),
                          ("sentence_transformers", "sentence_transformers")):
            try:
                env[name] = __import__(mod).__version__
            except Exception as exc:                               # noqa: BLE001 - recorded
                env[name] = "unavailable: %r" % (exc,)
        try:
            env["cuda"] = __import__("torch").version.cuda
        except Exception as exc:                                   # noqa: BLE001 - recorded
            env["cuda"] = "unavailable: %r" % (exc,)
    return env


def extracted_candidates(run_json_path, seed, hh, contract="contract2"):
    """One household's EXTRACTED candidates, shaped like the corpus's, for the MS1b winner run.

    The extractor's derived candidate already carries everything the store needs except the two
    fields that come from the span rather than from the utterance - the day it was said and its
    timestamp - so they are taken from the span the candidate cites. A candidate citing a span this
    household does not have is dropped rather than guessed at; none has been seen.

    The growth FILLER is deliberately untouched: it is ingested directly by the harness and is not
    an extraction, so replacing the oracle's candidates leaves the growth set intact and the growth
    band still measures what it measured before.
    """
    with open(run_json_path, encoding="utf-8") as fh:
        d = json.load(fh)
    # THE RUN AND THE CORPUS MUST BE THE SAME MEASUREMENT. A contract-2 run's predictions cite
    # contract-2 span ids; replaying them against a contract-3 corpus (or the reverse) would score a
    # model on spans it never saw and silently drop the rest. Each of these raises NAMING THE FILE,
    # because the wrong `--candidates-from` is the easy mistake here and a quiet empty list is the
    # worst way to find out.
    if d.get("contract", "contract2") != contract:
        raise ValueError("%s is contract %r, but the harness is running %r - a run's candidates are "
                         "only replayable against the corpus they were extracted from"
                         % (run_json_path, d.get("contract", "contract2"), contract))
    if d.get("days") is not None and d["days"] != hh["days"]:
        raise ValueError("%s ran %s days, the harness %s - different corpora"
                         % (run_json_path, d["days"], hh["days"]))
    rec = next((h for h in d["households"] if h["seed"] == seed), None)
    if rec is None:
        raise ValueError("%s holds no household for seed %s" % (run_json_path, seed))
    if rec.get("n_spans") is not None and rec["n_spans"] != len(hh["spans"]):
        raise ValueError("%s seed %s recorded %s spans, the corpus has %s - different corpora"
                         % (run_json_path, seed, rec["n_spans"], len(hh["spans"])))
    by_sid = {sp["sid"]: sp for sp in hh["spans"]}
    out = []
    for c in rec.get("predictions", []):
        sids = c.get("span_ids") or []
        sp = by_sid.get(sids[0]) if sids else None
        if sp is None:
            continue
        c = dict(c)
        c["day"] = sp["day"]
        c["said_at"] = sp["said_at"]
        out.append(c)
    return out


def _candidate_pronoun(cand, heard_by_day):
    """Resolve a third-person pronoun in a candidate's subject ref or relation object (MS2a-2).

    The design's §5, amended (1): an extractor says what the utterance said, so `she` reaches the
    harness as a subject ref or as a relation's object, and it means whoever was in the room. It is
    resolved on the date of the span the candidate CITES, by the same rule the spans use.

    Returns (candidate, outcome) where outcome is 'none' (no pronoun to resolve), 'resolved' (a
    COPY carrying cluster ids as strings) or 'unresolved'. Resolving REWRITES the ref, so a
    candidate held pending and re-offered on a later day is not resolved a second time against a
    different day's room - the pronoun is gone after the first pass, which is the mechanism rather
    than a flag to keep in step.
    """
    fields = []
    ref = (cand.get("subject") or {}).get("ref")
    if ref is not None and str(ref).strip().lower() in _people.THIRD_PERSON_PRONOUNS:
        fields.append("subject")
    if cand.get("predicate_id") == _EDGE_PREDICATE:
        obj = cand.get("object")
        if obj is not None and str(obj).strip().lower() in _people.THIRD_PERSON_PRONOUNS:
            fields.append("object")
    if not fields:
        return cand, "none"
    cluster = _people.resolve_pronoun(cand.get("day"), heard_by_day)
    if cluster is None:
        return cand, "unresolved"
    out = dict(cand)
    if "subject" in fields:
        subj = dict(out["subject"])
        subj["ref"] = str(cluster)
        out["subject"] = subj
    if "object" in fields:
        out["object"] = str(cluster)
    return out, "resolved"


def run_household(seed, days, predicate_hint=True, embedder=None, drop_stopwords=True,
                  candidates_from=None, contract="contract2", people_layer=False):
    """`predicate_hint=False` is the NEGATIVE CONTROL: the MS0 lane, unrestricted.

    `embedder` adds the vector lane. It is applied by `embed_pending` AFTER ingest, never during:
    the write path stays embedding-free so the p99 write band measures the store, not a GPU.

    `contract` selects the corpus and, on the EXTRACTED path only, the hearsay rule - which runs
    under contract 3 AND contract 4, because the design's amendment of 2026-09-23 says contract 4
    changes only the prompt and the derivation and so inherits every other contract-3 rule.

    `people_layer` runs MS2a-2's evidence rules over the spine and resolves pronouns in extracted
    candidates. It is a SWITCH rather than a new default so that every store run taken before it -
    MS0, MS0.1, MS1a.4, MS1b, MS2a-1 - stays re-runnable byte for byte; the new scoring fields and
    merge by rank are unconditional and were measured to move none of them.
    """
    hint = "auto" if predicate_hint else None
    hh = _corpus.generate_household(seed, days, contract)
    st = MemoryStore(":memory:", drop_stopwords=drop_stopwords)
    clusters = {c: st.add_cluster() for c in hh["clusters"]}
    owner_name = hh["persons"][0]["name"]
    partner_name = hh["persons"][1]["name"]
    ids = {"owner": st.bind_owner(clusters[1], owner_name)}

    spans_by_day = {}
    for sp in hh["spans"]:
        spans_by_day.setdefault(sp["day"], []).append(sp)
    # The ORACLE's candidates are the default and the path every earlier milestone measured.
    # `candidates_from` swaps in an MS1b run's EXTRACTED candidates instead - the same store, the
    # same rules, the same bands, driven by what a model actually produced.
    source = (extracted_candidates(candidates_from, seed, hh, contract) if candidates_from
              else hh["candidates"])
    alias = None
    # The HEARSAY map (contract 3 f): a subject ref to the cluster that ref denotes, in BOTH the
    # forms an extractor produces - the cluster id as a string (`derive._subject` for a first-person
    # `about`) and each household member's own lower-cased name. Built here because only the
    # household knows its names, which is the review finding that put this map in the harness.
    cluster_of_ref = {"1": 1, "2": 2,
                      str(owner_name).lower(): 1, str(partner_name).lower(): 2}
    hearsay_demoted = 0
    if candidates_from:
        # An extractor names a subject the way the utterance did: a cluster id or a name. Both mean
        # one of the two people the oracle calls "owner" and "partner".
        alias = {"1": "owner", "2": "partner",
                 str(owner_name).lower(): "owner", str(partner_name).lower(): "partner"}
    cands_by_day = {}
    for c in source:
        cands_by_day.setdefault(c["day"], []).append(c)

    # Which non-owner CORPUS clusters were heard on each corpus day (the owner is cluster 1). Built
    # once: `resolve_pronoun` reads only the candidate's own day and the three before it, all of
    # which are days already replayed by the time a candidate for that day is offered, so a map
    # built up front is identical to one grown day by day.
    heard_by_corpus_day = {}
    for sp in hh["spans"]:
        if sp["cluster"] != 1:
            heard_by_corpus_day.setdefault(sp["day"], set()).add(sp["cluster"])

    sid_map, pending = {}, []
    spouse_day = None
    relationship_day = None
    pronouns_resolved = 0
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
            # PRONOUN RESOLUTION RUNS FIRST - before `_prepare` and before the hearsay check -
            # because both of those read the subject ref, and `she` is not a subject either of them
            # can decide anything about. This is the milestone the MS2a-1 comment below anticipated.
            if people_layer and candidates_from:
                c, outcome = _candidate_pronoun(c, heard_by_corpus_day)
                if outcome == "resolved":
                    pronouns_resolved += 1
                elif outcome == "unresolved":
                    # Dropped, never held pending: a pronoun that did not resolve on its own day
                    # will not resolve on a later one (the rule reads the cited span's date), so
                    # holding it would leave it pending forever and count as a measurement that
                    # never happened. The loss is audited, which is what makes it a loss and not a
                    # silent drop.
                    st._audit("reject", "candidate", rule="people",
                              note="unresolved pronoun %r on %s citing span(s) %s"
                                   % ((c.get("subject") or {}).get("ref"), c.get("said_at"),
                                      c.get("span_ids")))
                    st.conn.commit()
                    continue
            # THE HEARSAY CHECK RUNS HERE, when the candidate is first offered, and not inside
            # `extracted_candidates`: a later milestone resolves a pronoun ref to a cluster, and
            # that resolution has to happen before this decision, not after it. Contract-3 and
            # contract-4 EXTRACTED runs only - the oracle path and every contract-2 store run are
            # untouched, so every earlier number stays re-runnable. Contract 4 is here because the
            # design's amendment of 2026-09-23 says it changes only the prompt and the derivation:
            # keyed on contract 3 by equality, a contract-4 run would ingest as STATED what contract
            # 3 demotes, and its bands would move for a harness reason rather than a prompt one.
            if (contract in ("contract3", "contract4") and candidates_from
                    and not _people.stated_allowed(c, cluster_of_ref)):
                c = dict(c)
                c["source_kind"] = "inferred"
                c["stated"] = False
                hearsay_demoted += 1
            prepared = _prepare(c, ids, sid_map, alias)
            if prepared is None:
                pending.append(c)
                continue
            st.ingest(prepared)

        # The evidence rules run AFTER the day's candidates and BEFORE the day's checks, so the
        # surfacing day a check reads is the day the store would have surfaced it to the owner.
        if people_layer:
            st.apply_evidence_rules(ids["owner"], clusters[1],
                                    _corpus._said_at(day, 0)[:10])

        conf = _spouse_confidence(st, ids.get("owner"), ids.get("partner"))
        if spouse_day is None and conf is not None and conf >= SURFACE_THRESHOLD:
            spouse_day = day
        # THE RELATIONSHIP, as the design's amended band asks for it: `spouse` OR the coarser
        # `partner`, whichever surfaces first. Tracked beside the spouse day and never instead of
        # it - the two answer different questions and `spouse` stays separately REPORTED.
        pconf = _edge_confidence(st, ids.get("owner"), ids.get("partner"), "partner")
        best = max([x for x in (conf, pconf) if x is not None], default=None)
        if relationship_day is None and best is not None and best >= SURFACE_THRESHOLD:
            relationship_day = day

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
    rel = _score_relations(st, hh["sets"]["relations"], ids)
    spouse_conf = _spouse_confidence(st, ids.get("owner"), ids.get("partner"))
    partner_conf = _edge_confidence(st, ids.get("owner"), ids.get("partner"), "partner")
    finest = _people.finest_surfaced(
        [r for r in _surfaced_edges(st)
         if r["from_person"] == ids.get("owner") and r["to_person"] == ids.get("partner")])
    relationship_finest = ({"relation_id": finest["relation_id"],
                            "source_kind": finest["source_kind"],
                            "confidence": round(finest["confidence"], 4)}
                           if finest else None)

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
    # The people layer's own trail, read before the connection closes. Counted from the audit table
    # rather than tallied in Python as the run goes: the rows ARE the record, so a counter that
    # disagreed with them would be the thing that is wrong.
    rejects_by_table = {"span": 0, "candidate": 0}
    for r in st.conn.execute(
            "select target_table, count(*) n from audit where op='reject' and rule='people' "
            "group by target_table").fetchall():
        rejects_by_table[r["target_table"]] = r["n"]
    people_rejects = sum(rejects_by_table.values())
    rank_upgrades = st.conn.execute(
        "select count(*) from audit where op='upgrade'").fetchone()[0]
    # REPORTED, never banded: a current edge sitting on a slot that also holds a CLOSED row means
    # the rules re-derived an edge the store had closed. Zero on every path measured so far; it is
    # surfaced so that a future corpus where it is not zero says so rather than looking clean.
    edges_reopened = st.conn.execute(
        "select count(*) from edge e where e.valid_to is null and exists ("
        "  select 1 from edge o where o.valid_to is not null"
        "   and o.from_person = e.from_person and o.to_person = e.to_person"
        "   and o.relation_id = e.relation_id)").fetchone()[0]
    st.close()
    out_hh = {
        "seed": seed,
        "n_spans": len(hh["spans"]),
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
        "relation_precision": round(rel["relation_precision"], 4),
        "relations_surfaced": rel["relations_surfaced"],
        # MS2a-2. The pair figure is the MS2 band's; `relation_precision` above is the historical
        # per-edge one and keeps its meaning. Printed side by side on purpose - the log must never
        # let a reader take one for the other.
        "relation_precision_pairs": (round(rel["relation_precision_pairs"], 4)
                                     if rel["relation_precision_pairs"] is not None else None),
        "relations_surfaced_pairs": rel["relations_surfaced_pairs"],
        "relations_fine": rel["relations_fine"],
        "relations_coarse": rel["relations_coarse"],
        "relations_wrong": rel["relations_wrong"],
        "spouse_surfaced_day": spouse_day,
        "spouse_confidence_last_day": round(spouse_conf, 4) if spouse_conf is not None else None,
        "partner_confidence_last_day": (round(partner_conf, 4)
                                        if partner_conf is not None else None),
        "relationship_surfaced_day": relationship_day,
        "relationship_finest": relationship_finest,
        "people_rejects": people_rejects,
        "people_rejects_by_table": rejects_by_table,
        "edges_reopened": edges_reopened,
        "rank_upgrades": rank_upgrades,
        "pronouns_resolved": pronouns_resolved,
        "pending_at_end": len(pending),
        "growth_update_acc": round(growth_acc, 4),
        "growth_drop_points": round(100.0 * (update_acc - growth_acc), 4),
        "growth_update_acc_paraphrase": round(growth_para, 4),
        "growth_drop_paraphrase_points": round(100.0 * (update_para - growth_para), 4),
        "audit_violations": violations,
    }
    # EXTRACTED-path runs only, and 0 under contract 2 by construction. An ORACLE output carries no
    # such key at all, so no earlier control file gains a field.
    if candidates_from:
        out_hh["hearsay_demoted"] = hearsay_demoted
    return out_hh


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
        embedder_name="none", drop_stopwords=True, candidates_from=None,
        contract="contract2", people_layer=False) -> dict:
    households = [run_household(s, days, predicate_hint, embedder, drop_stopwords, candidates_from,
                                contract, people_layer)
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

    # MS2a-2 aggregates. The pair precision is POOLED, not a mean of per-household means: a
    # household that surfaced one pair and a household that surfaced three are not equal evidence,
    # and the band is a statement about the surfaced pairs rather than about the households.
    for field in ("relations_fine", "relations_coarse", "relations_wrong",
                  "relations_surfaced_pairs", "people_rejects", "edges_reopened",
                  "rank_upgrades", "pronouns_resolved", "pending_at_end"):
        agg[field] = sum(h[field] for h in households)
    agg["relation_precision_pairs"] = (
        round((agg["relations_fine"] + agg["relations_coarse"])
              / agg["relations_surfaced_pairs"], 4)
        if agg["relations_surfaced_pairs"] else None)
    rel_days = [h["relationship_surfaced_day"] for h in households
                if h["relationship_surfaced_day"] is not None]
    agg["relationship_surfaced_day_mean"] = (round(statistics.fmean(rel_days), 4)
                                             if rel_days else None)
    agg["relationship_surfaced_households"] = f"{len(rel_days)}/{len(households)}"
    # The COUNT beside the "k/n" string, because `ms2_bands` has to compare it against a fraction
    # of the households and parsing a display string to get a number back is how a band starts
    # depending on a format.
    agg["relationship_surfaced_count"] = len(rel_days)

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
        "people_layer": bool(people_layer),
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
        # The MS2 band list, written on every run so a lane-OFF or layer-OFF file can still be read
        # beside a banded one. The MS0 `bands` block above is UNTOUCHED: MS0's thresholds and MS2's
        # are different questions asked of the same store, and collapsing them would silently
        # re-base every earlier milestone's verdict.
        "ms2_bands": ms2_bands(agg, len(households), violations, latency, embedder_name),
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
    out["contract"] = contract
    if candidates_from:
        # WHICH run's candidates these are, and under what contract and schema they were produced -
        # so a reader of this file never has to guess which extraction it is scoring.
        out["hearsay_demoted"] = sum(h.get("hearsay_demoted", 0) for h in households)
        try:
            with open(candidates_from, encoding="utf-8") as fh:
                src = json.load(fh)
            out["candidates_contract"] = src.get("contract")
            out["candidates_schema_sha256"] = src.get("schema_sha256")
        except Exception as exc:                                   # noqa: BLE001 - recorded
            out["candidates_contract"] = "unavailable: %r" % (exc,)
            out["candidates_schema_sha256"] = None
    if out_path:
        out["env"] = _env_block(embedder)
        with open(out_path, "w", encoding="utf-8") as fh:
            json.dump(out, fh, indent=2, sort_keys=True)
            fh.write("\n")
    return out
