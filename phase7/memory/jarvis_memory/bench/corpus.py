"""A seeded template household: the transcript spans and the ORACLE candidates over them.

`generate_household(seed, days)` is deterministic — the same seed gives byte-identical JSON — and it
plants ground truth the harness can score against:

  * an owner (cluster 1, enrolled) and a partner (cluster 2) who earns personhood on day 3, plus a
    visitor (cluster 3) heard on exactly two days who never does;
  * four single-valued slots, two of them carrying a DATED UPDATE (the owner moves on day 9, the
    partner changes job on day 12) - the knowledge-update set;
  * multi-valued habits and routines, one habit ENDED by an owner statement on day 10 - the
    coexisting set, whose teeth are that the ended value must not come back;
  * an owner-to-partner `spouse` edge built only from INFERRED evidence on days 1, 3, 5, 6, 8, 12
    and 13 with one CONTRADICTING day on day 11 - the guess, and the reason the surfacing day is a
    measurement rather than an assertion; and the partner-to-owner edge stated by her, for contrast;
  * four owner preferences, one of which flips polarity on day 7 (the old value ENDED, both rows
    kept) - the transfer set's targets;
  * filler candidates about disjoint people and places for the growth set.

The transfer scenarios come from SYNONYM_SCENARIOS below, a fixed table whose entries share no word
with the preference they point at. That disjointness is asserted by the test suite, not assumed
here, because it is the whole substance of the transfer measurement: a scenario the full-text lane
could match on a shared word would measure nothing.

Standard library only.
"""
import random

# ---------------------------------------------------------------- vocabularies
NAMES = ["sam", "alex", "jo", "kit", "robin", "morgan", "casey", "riley", "quinn", "harper",
         "elliot", "frankie", "jamie", "logan", "reese", "sage", "toni", "vic", "wren", "zane"]
PARTNER_NAMES = ["erin", "dana", "noor", "leah", "priya", "mia", "ava", "iris", "nadia", "rosa",
                 "tess", "yara", "beth", "cleo", "esme", "gina", "hana", "juno", "kira", "lena"]
CITIES = ["brisbane", "sydney", "perth", "hobart", "adelaide", "darwin", "cairns", "geelong",
          "ballarat", "bendigo", "launceston", "toowoomba"]
JOBS = ["nurse", "plumber", "teacher", "chef", "electrician", "librarian", "paramedic",
        "carpenter", "pharmacist", "surveyor"]
HABITS = ["runs at dawn", "swims on sundays", "cycles to work", "bakes on fridays",
          "walks the dog at seven", "reads before bed"]
ROUTINES = ["bins go out on tuesday", "groceries arrive thursday", "washing on saturday"]
TOPICS = ["gardening", "cricket", "renovations", "camping", "pottery", "birdwatching",
          "cycling", "astronomy"]

# The four preference topics and, for each, three scenario queries that share NO word with it.
# A scenario is what the owner would ask in a situation where the preference ought to be recalled.
SYNONYM_SCENARIOS = {
    "spicy food": [
        "what should i order at the restaurant tonight",
        "planning dinner for our anniversary",
        "which cuisine would suit us both",
    ],
    "loud music": [
        "picking a venue for the party",
        "is that bar going to be too much",
        "choosing background sound for the evening",
    ],
    "early mornings": [
        "when should we schedule the appointment",
        "is a sunrise start reasonable",
        "booking a flight time that works",
    ],
    "long drives": [
        "how should we travel to the coast",
        "would a train be better than the car",
        "planning the route for our holiday",
    ],
}
PREF_TOPICS = list(SYNONYM_SCENARIOS)

# Filler vocabulary, deliberately disjoint from everything above.
FILLER_NAMES = ["ulric", "opal", "pascal", "quilla", "ronan", "solveig", "tarquin", "ursula",
                "verity", "wilhelm", "xanthe", "yusuf", "zora", "ambrose", "bianca"]
FILLER_PLACES = ["reykjavik", "tromso", "valparaiso", "windhoek", "yakutsk", "zanzibar",
                 "bergen", "cusco", "dakar", "esbjerg", "faro", "gdansk"]
FILLER_JOBS = ["glassblower", "cartographer", "luthier", "falconer", "milliner", "cooper",
               "wheelwright", "thatcher"]


def _cand(predicate_id, subject_ref, subject_kind, obj, obj_norm, source_kind, speaker_cluster,
          span_ids, day, said_at, **over):
    c = {
        "predicate_id": predicate_id,
        "subject": {"kind": subject_kind, "ref": subject_ref},
        "object": obj,
        "object_norm": obj_norm,
        "source_kind": source_kind,
        "speaker_cluster": speaker_cluster,
        "span_ids": list(span_ids),
        "about_time": None,
        "relation_id": None,
        "polarity": None,
        "strength": None,
        "ended": False,
        "said_at": said_at,
        "day": day,
    }
    c.update(over)
    return c


def _said_at(day, seconds):
    """Day 1 is 2026-03-01; the clock inside a day only has to be monotonic."""
    from datetime import datetime, timedelta
    base = datetime(2026, 3, 1, 8, 0, 0) + timedelta(days=day - 1, seconds=seconds)
    return base.isoformat(timespec="seconds")


def generate_household(seed: int, days: int = 14) -> dict:
    """A deterministic template household. Same seed, same bytes."""
    rng = random.Random(seed)
    owner_name = NAMES[seed % len(NAMES)]
    partner_name = PARTNER_NAMES[(seed * 7 + 3) % len(PARTNER_NAMES)]

    cities = rng.sample(CITIES, 4)
    jobs = rng.sample(JOBS, 4)
    habits = rng.sample(HABITS, 5)
    routines = rng.sample(ROUTINES, 2)
    topics = rng.sample(TOPICS, 6)

    spans = []

    def add_span(day, cluster, text, sec):
        sid = len(spans)
        spans.append({"sid": sid, "day": day, "cluster": cluster, "text": text,
                      "said_at": _said_at(day, sec)})
        return sid

    # --- the transcript ------------------------------------------------------
    # The owner speaks every day; the partner on ten days with five spans each, so she earns
    # personhood at the end of day 3; the visitor on exactly two days and never does.
    owner_days = list(range(1, days + 1))
    partner_days = [d for d in (1, 2, 3, 4, 5, 6, 8, 10, 12, 13) if d <= days]
    visitor_days = [d for d in (7, 9) if d <= days]

    chatter = ["morning", "did you see that", "i will sort it out", "not sure yet",
               "sounds good to me", "later today", "we should decide", "it can wait"]
    for d in owner_days:
        for i in range(5):
            add_span(d, 1, f"{chatter[(d + i) % len(chatter)]}", 100 + i * 10)
    for d in partner_days:
        for i in range(5):
            add_span(d, 2, f"{chatter[(d + i + 3) % len(chatter)]}", 200 + i * 10)
    for d in visitor_days:
        for i in range(5):
            add_span(d, 3, f"just passing through {chatter[(d + i) % len(chatter)]}", 300 + i * 10)

    cands = []

    def utter(day, cluster, text, sec):
        return add_span(day, cluster, text, sec)

    # --- single-valued facts, two of them updated ---------------------------
    s = utter(2, 1, f"we live in {cities[0]} at the moment", 400)
    cands.append(_cand("person.lives_in", "owner", "person", cities[0].title(), cities[0],
                       "stated_owner", 1, [s], 2, _said_at(2, 400)))
    s = utter(9, 1, f"we moved to {cities[1]} last week", 400)
    cands.append(_cand("person.lives_in", "owner", "person", cities[1].title(), cities[1],
                       "stated_owner", 1, [s], 9, _said_at(9, 400)))

    s = utter(4, 2, f"i live in {cities[2]} now", 400)
    cands.append(_cand("person.lives_in", "partner", "person", cities[2].title(), cities[2],
                       "stated_other", 2, [s], 4, _said_at(4, 400)))

    s = utter(4, 1, f"i work as a {jobs[0]}", 410)
    cands.append(_cand("person.works_as", "owner", "person", f"a {jobs[0]}", jobs[0],
                       "stated_owner", 1, [s], 4, _said_at(4, 410)))
    s = utter(4, 2, f"i work as a {jobs[1]}", 420)
    cands.append(_cand("person.works_as", "partner", "person", f"a {jobs[1]}", jobs[1],
                       "stated_other", 2, [s], 4, _said_at(4, 420)))
    s = utter(12, 2, f"i started as a {jobs[2]} this month", 420)
    cands.append(_cand("person.works_as", "partner", "person", f"a {jobs[2]}", jobs[2],
                       "stated_other", 2, [s], 12, _said_at(12, 420)))

    # --- multi-valued: three owner habits, one ended on day 10 ---------------
    for i, h in enumerate(habits[:3]):
        s = utter(5 + (i // 2), 1, f"i still {h}", 430 + i * 5)
        cands.append(_cand("person.habit", "owner", "person", h, h, "stated_owner", 1, [s],
                           5 + (i // 2), _said_at(5 + (i // 2), 430 + i * 5)))
    s = utter(10, 1, f"i stopped, i no longer {habits[0]}", 440)
    cands.append(_cand("person.habit", "owner", "person", habits[0], habits[0], "stated_owner", 1,
                       [s], 10, _said_at(10, 440), ended=True))

    for i, h in enumerate(habits[3:5]):
        s = utter(6, 2, f"i {h}", 450 + i * 5)
        cands.append(_cand("person.habit", "partner", "person", h, h, "stated_other", 2, [s], 6,
                           _said_at(6, 450 + i * 5)))

    for i, r in enumerate(routines):
        s = utter(7 + i, 1, f"remember the {r}", 460 + i * 5)
        cands.append(_cand("household.routine", None, "household", r, r, "stated_owner", 1, [s],
                           7 + i, _said_at(7 + i, 460 + i * 5)))

    for i, t in enumerate(topics):
        day = 1 + (i * 2) % max(1, days - 1)
        s = utter(day, 1, f"we were talking about {t} again", 470 + i * 5)
        cands.append(_cand("household.topic", None, "household", t, t, "stated_owner", 1, [s], day,
                           _said_at(day, 470 + i * 5)))

    # --- the guess: an INFERRED spouse edge that accrues over days -----------
    # Days 1, 3, 5, 6, 8 give five supporting days, so it crosses 0.80 on day 8; day 11 contradicts
    # (back to 0.7364) and days 12 and 13 carry it to 0.8647. None of this is stated by the owner.
    support_days = [d for d in (1, 3, 5, 6, 8, 12, 13) if d <= days]
    hints = ["she was here all evening again", "we sorted the bills together",
             "she picked the kids up", "we are both on the lease", "she called me love",
             "we shared the school run", "we planned the week together"]
    for i, d in enumerate(support_days):
        s = utter(d, 1, hints[i % len(hints)], 500 + i)
        cands.append(_cand("person.relation_to", "owner", "person", "partner", "spouse",
                           "inferred", 1, [s], d, _said_at(d, 500 + i), relation_id="spouse"))
    if days >= 11:
        s = utter(11, 1, "she said she is just staying with us for now", 501)
        cands.append(_cand("person.relation_to", "owner", "person", "partner", "spouse",
                           "inferred", 1, [support_days and spans[0]["sid"] or 0], 11,
                           _said_at(11, 501), relation_id="spouse", contradicts=[s]))

    for i, d in enumerate([d for d in (4, 6, 9) if d <= days]):
        s = utter(d, 2, f"my husband {owner_name} and i decided", 510 + i)
        cands.append(_cand("person.relation_to", "partner", "person", "owner", "spouse",
                           "stated_other", 2, [s], d, _said_at(d, 510 + i), relation_id="spouse"))

    # --- preferences: four, one flipping polarity on day 7 -------------------
    pref_days = [1, 3, 5, 6]
    for i, topic in enumerate(PREF_TOPICS):
        d = pref_days[i]
        pol = ["likes", "likes", "wants", "avoids"][i]
        s = utter(d, 1, f"i really do enjoy {topic}", 520 + i)
        cands.append(_cand("owner.prefers", "owner", "person", topic, topic, "stated_owner", 1,
                           [s], d, _said_at(d, 520 + i), polarity=pol, strength=2))
    if days >= 7:
        s = utter(7, 1, f"i have gone off {PREF_TOPICS[0]} entirely", 530)
        cands.append(_cand("owner.prefers", "owner", "person", PREF_TOPICS[0], PREF_TOPICS[0],
                           "stated_owner", 1, [s], 7, _said_at(7, 530),
                           polarity="dislikes", strength=2))
        s2 = utter(7, 1, f"i no longer enjoy {PREF_TOPICS[0]}", 531)
        cands.append(_cand("owner.prefers", "owner", "person", PREF_TOPICS[0], PREF_TOPICS[0],
                           "stated_owner", 1, [s2], 7, _said_at(7, 531),
                           polarity="likes", strength=2, ended=True))

    # --- the scored sets -----------------------------------------------------
    update = []
    for ref, name, pred, gold in (("owner", owner_name, "person.lives_in", cities[1]),
                                  ("partner", partner_name, "person.lives_in", cities[2]),
                                  ("owner", owner_name, "person.works_as", jobs[0]),
                                  ("partner", partner_name, "person.works_as", jobs[2])):
        if pred == "person.lives_in":
            qs = [f"where does {name} live", f"which city does {name} live in"]
        else:
            qs = [f"what does {name} do for work", f"what job does {name} work as"]
        for q in qs:
            update.append({"query": q, "gold_object_norm": gold, "subject": ref,
                           "predicate_id": pred})

    coexist = [
        {"query": f"what habits does {owner_name} have", "subject": "owner",
         "predicate_id": "person.habit",
         "gold_object_norms": sorted(habits[1:3]), "ended_object_norm": habits[0]},
        {"query": f"what habits does {partner_name} have", "subject": "partner",
         "predicate_id": "person.habit",
         "gold_object_norms": sorted(habits[3:5]), "ended_object_norm": None},
        {"query": "what household routine do we keep", "subject": None,
         "predicate_id": "household.routine",
         "gold_object_norms": sorted(routines), "ended_object_norm": None},
    ]

    transfer = []
    for topic in PREF_TOPICS:
        for q in SYNONYM_SCENARIOS[topic]:
            transfer.append({"query": q, "gold_topic_norm": topic})

    relations = [
        {"from": "owner", "to": "partner", "relation_id": "spouse"},
        {"from": "partner", "to": "owner", "relation_id": "spouse"},
    ]

    # --- the growth filler ---------------------------------------------------
    # 30x the gold candidates, about people and places that appear nowhere above, so the growth set
    # measures dilution of the index rather than a collision with the gold facts.
    filler = []
    target = 30 * len(cands)
    i = 0
    while len(filler) < target:
        pname = f"{FILLER_NAMES[i % len(FILLER_NAMES)]}{i}"
        place = FILLER_PLACES[(i * 3) % len(FILLER_PLACES)]
        job = FILLER_JOBS[(i * 5) % len(FILLER_JOBS)]
        day = 1 + (i % days)
        filler.append({"predicate_id": "person.lives_in", "person_ref": pname,
                       "object": place.title(), "object_norm": place,
                       "span_text": f"{pname} lives in {place}", "day": day,
                       "said_at": _said_at(day, 600 + (i % 300))})
        if len(filler) < target:
            filler.append({"predicate_id": "person.works_as", "person_ref": pname,
                           "object": f"a {job}", "object_norm": job,
                           "span_text": f"{pname} works as a {job}", "day": day,
                           "said_at": _said_at(day, 601 + (i % 300))})
        i += 1

    return {
        "seed": seed,
        "days": days,
        "persons": [{"ref": "owner", "name": owner_name, "cluster": 1},
                    {"ref": "partner", "name": partner_name, "cluster": 2}],
        "clusters": [1, 2, 3],
        "spans": spans,
        "candidates": cands,
        "sets": {"update": update, "coexist": coexist, "transfer": transfer,
                 "relations": relations, "growth_filler": filler},
    }
