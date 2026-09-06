"""MemoryStore — the SQLite spine, and the one place the rules are applied to real rows.

The design's §3, §4 and §6. Every write goes through `ingest`, which validates the candidate,
routes it to fact / edge / preference, asks `rules.decide` what to do, and applies the whole answer
inside ONE transaction together with its audit rows. There is no other write path, so "every loss is
audited" is checkable by walking the tables — which `audit_violations` does.

Three things here are decisions rather than transcription, and each is marked in the code:

  * **Evidence accrues onto a multi-valued row rather than duplicating it.** R4 says a new candidate
    on a multi-valued predicate is another current row - that is about a new VALUE (a second habit
    coexists with the first). Repeated evidence for the SAME value is not a second belief, and R5
    recomputes confidence from a row's spans, so the spans have to land on the row that holds the
    belief. Without this the owner-to-partner spouse edge would be seven one-span rows at 0.28
    apiece instead of one row climbing past the 0.80 surfacing threshold on its fifth day.
  * **A preference's identity is (topic, polarity), not topic alone.** "likes spicy food" and
    "dislikes spicy food" are two rows on one slot, which is what lets the console show
    "used to like X, now avoids it" instead of a silent overwrite.
  * **A span in the full-text lane is evidence, not a claim**, so it is ranked as `inferred` and
    decays. That is why a stated fact outranks the raw utterance it was extracted from.

Standard library only: sqlite3, datetime, math via the pure modules. No model, no GPU, no numpy.
"""
import datetime as _dt
import sqlite3

from . import people as _people
from . import retrieve as _retrieve
from .candidate import validate
from .confidence import confidence as _confidence, distinct_days
from .paths import default_db
from .registry import (
    EDGE_PREDICATE, PREFERENCE_PREDICATE, arity, is_known, predicate_words,
)
from .rules import decide
from .schema import DDL

# The three versioned belief tables and the span table that carries their evidence.
BELIEF_TABLES = ("fact", "edge", "preference")
SPAN_LINK = {"fact": ("fact_span", "fact_id"), "edge": ("edge_span", "edge_id"),
             "preference": ("preference_span", "preference_id"), "event": ("event_span", "event_id")}


def fts5_available(conn) -> bool:
    """Whether this SQLite build carries FTS5. Split out so a test can replace it: the refusal
    path is the one branch a working machine can never reach on its own."""
    rows = conn.execute("select * from pragma_compile_options").fetchall()
    return any("ENABLE_FTS5" in str(r[0]) for r in rows)


def _iso(ts) -> str:
    return ts.isoformat(timespec="seconds") if isinstance(ts, _dt.datetime) else str(ts)


def _now_iso() -> str:
    return _dt.datetime.now().replace(microsecond=0).isoformat()


def _tokens(text: str):
    """The query words, lower-cased, alphanumeric runs only. FTS5 syntax characters never survive
    this, so an operator question can never be read as a MATCH expression."""
    out, cur = [], []
    for ch in str(text).lower():
        if ch.isalnum():
            cur.append(ch)
        elif cur:
            out.append("".join(cur))
            cur = []
    if cur:
        out.append("".join(cur))
    return out


class MemoryStore:
    """The household store. `path` None uses JARVIS_MEMORY_HOME (else %USERPROFILE%\\.jarvis\\
    memory\\household.sqlite); ':memory:' is accepted for tests and the benchmark."""

    def __init__(self, path=None):
        self.path = ":memory:" if path == ":memory:" else str(path or default_db())
        self.conn = sqlite3.connect(self.path)
        self.conn.row_factory = sqlite3.Row
        if not fts5_available(self.conn):
            self.conn.close()
            raise RuntimeError(
                "this SQLite build has no FTS5 (pragma_compile_options lacks ENABLE_FTS5); "
                "the memory store's full-text lane cannot run without it")
        if self.path != ":memory:":
            self.conn.execute("PRAGMA journal_mode=WAL")
            self.conn.execute("PRAGMA synchronous=NORMAL")
        self.conn.executescript(DDL)
        self.conn.commit()

    # ------------------------------------------------------------------ close
    def close(self):
        self.conn.close()

    # ------------------------------------------------------------- the spine
    def add_recording(self, sha256, started_at, duration_s, device) -> int:
        cur = self.conn.execute(
            "insert into recording (sha256, started_at, duration_s, device, transcribed_at) "
            "values (?,?,?,?,?)", (sha256, _iso(started_at), duration_s, device, _now_iso()))
        self.conn.commit()
        return cur.lastrowid

    def add_cluster(self, centroid=None) -> int:
        cur = self.conn.execute("insert into cluster (centroid) values (?)", (centroid,))
        self.conn.commit()
        return cur.lastrowid

    def add_span(self, recording_id, t_start_s, t_end_s, cluster_id, text, asr_conf,
                 about_time=None, about_time_source=None) -> int:
        started = self.conn.execute(
            "select started_at from recording where id=?", (recording_id,)).fetchone()
        if started is None:
            raise ValueError(f"no recording {recording_id}")
        said_at = _iso(_dt.datetime.fromisoformat(started[0]) + _dt.timedelta(seconds=float(t_start_s)))
        cur = self.conn.execute(
            "insert into span (recording_id, t_start_s, t_end_s, cluster_id, text, asr_conf, "
            "said_at, about_time, about_time_source) values (?,?,?,?,?,?,?,?,?)",
            (recording_id, t_start_s, t_end_s, cluster_id, text, asr_conf, said_at,
             about_time, about_time_source))
        span_id = cur.lastrowid
        self.conn.execute("insert into span_fts (rowid, text) values (?,?)", (span_id, text))
        self.conn.execute(
            "update cluster set n_spans = n_spans + 1, "
            "first_heard = coalesce(first_heard, ?) where id=?", (said_at, cluster_id))
        self.conn.commit()
        return span_id

    def bind_owner(self, cluster_id, display_name) -> int:
        cur = self.conn.execute(
            "insert into person (kind, display_name, name_confidence, name_source_kind, created_at) "
            "values ('owner',?,1.0,'stated_owner',?)", (display_name, _now_iso()))
        pid = cur.lastrowid
        self.conn.execute("update cluster set person_id=? where id=?", (pid, cluster_id))
        self.conn.commit()
        return pid

    def promote_persons(self) -> list:
        """Apply the §3.4 personhood rule to every cluster that is not yet a person."""
        new = []
        rows = self.conn.execute("select id from cluster where person_id is null").fetchall()
        for (cid,) in rows:
            days = self.conn.execute(
                "select substr(said_at,1,10) d, count(*) n from span where cluster_id=? group by d",
                (cid,)).fetchall()
            counts = {r[0]: r[1] for r in days}
            if not _people.is_person(counts):
                continue
            cur = self.conn.execute(
                "insert into person (kind, created_at) values ('cluster',?)", (_now_iso(),))
            pid = cur.lastrowid
            self.conn.execute("update cluster set person_id=?, days_heard=? where id=?",
                              (pid, len(counts), cid))
            new.append(pid)
        self.conn.commit()
        return new

    def person_for_cluster(self, cluster_id):
        r = self.conn.execute("select person_id from cluster where id=?", (cluster_id,)).fetchone()
        return r[0] if r else None

    def display_name(self, person_id) -> str:
        r = self.conn.execute("select display_name from person where id=?", (person_id,)).fetchone()
        return (r[0] if r and r[0] else "") or ""

    # ------------------------------------------------------------- the routing
    @staticmethod
    def route(predicate_id: str) -> str:
        if predicate_id == EDGE_PREDICATE:
            return "edge"
        if predicate_id == PREFERENCE_PREDICATE:
            return "preference"
        return "fact"

    def _slot(self, table, cand):
        if table == "edge":
            return {"from_person": cand["subject"].get("id"),
                    "to_person": int(cand["object"]),
                    "relation_id": cand.get("relation_id")}
        if table == "preference":
            return {"person_id": cand["subject"].get("id"), "topic_norm": cand.get("object_norm")}
        return {"subject_kind": cand["subject"].get("kind"),
                "subject_id": cand["subject"].get("id"),
                "predicate_id": cand["predicate_id"]}

    @staticmethod
    def _value_key(table, row):
        """What distinguishes two coexisting rows inside one slot. For a preference that is the
        polarity, not the topic - the topic is the slot."""
        if table == "preference":
            return f"{row.get('topic_norm')}|{row.get('polarity')}"
        if table == "edge":
            return str(row.get("relation_id"))
        return row.get("object_norm")

    def _cand_value_key(self, table, cand):
        if table == "preference":
            return f"{cand.get('object_norm')}|{cand.get('polarity')}"
        if table == "edge":
            return str(cand.get("relation_id"))
        return cand.get("object_norm")

    def current(self, table, **slot) -> list:
        where = " and ".join(f"{k}=?" for k in slot)
        sql = f"select * from {table} where valid_to is null"
        if where:
            sql += f" and {where}"
        sql += " order by id"
        return [dict(r) for r in self.conn.execute(sql, tuple(slot.values())).fetchall()]

    # -------------------------------------------------------------- the write
    def _span_clusters(self, span_ids):
        if not span_ids:
            return {}
        marks = ",".join("?" for _ in span_ids)
        rows = self.conn.execute(
            f"select id, cluster_id from span where id in ({marks})", tuple(span_ids)).fetchall()
        return {r[0]: r[1] for r in rows}

    def _link_spans(self, table, row_id, span_ids, role="support"):
        link, col = SPAN_LINK[table]
        for sid in span_ids or ():
            self.conn.execute(
                f"insert or ignore into {link} ({col}, span_id, role) values (?,?,?)",
                (row_id, sid, role))

    def _audit(self, op, target_table, loser_id=None, winner_id=None, rule=None, note=None) -> int:
        cur = self.conn.execute(
            "insert into audit (ts, op, target_table, loser_id, winner_id, rule, note) "
            "values (?,?,?,?,?,?,?)",
            (_now_iso(), op, target_table, loser_id, winner_id, rule, note))
        return cur.lastrowid

    def _fact_fts_text(self, subject_id, subject_kind, predicate_id, object_text) -> str:
        name = self.display_name(subject_id) if subject_kind == "person" else (subject_kind or "")
        return " ".join(x for x in (name, predicate_words(predicate_id), str(object_text or "")) if x)

    def _insert_row(self, table, cand, new_row, slot):
        vf = new_row.get("valid_from")
        if table == "edge":
            cur = self.conn.execute(
                "insert into edge (from_person, to_person, relation_id, source_kind, confidence, "
                "valid_from, valid_to, recorded_at, superseded_by) values (?,?,?,?,?,?,?,?,?)",
                (slot["from_person"], slot["to_person"], slot["relation_id"],
                 cand["source_kind"], 1.0, vf, new_row.get("valid_to"),
                 new_row["recorded_at"], new_row.get("superseded_by")))
            return cur.lastrowid
        if table == "preference":
            cur = self.conn.execute(
                "insert into preference (person_id, topic_norm, polarity, strength, source_kind, "
                "confidence, valid_from, valid_to, recorded_at, superseded_by) "
                "values (?,?,?,?,?,?,?,?,?,?)",
                (slot["person_id"], slot["topic_norm"], cand.get("polarity"), cand.get("strength"),
                 cand["source_kind"], 1.0, vf, new_row.get("valid_to"),
                 new_row["recorded_at"], new_row.get("superseded_by")))
            return cur.lastrowid
        cur = self.conn.execute(
            "insert into fact (subject_kind, subject_id, predicate_id, object_text, object_norm, "
            "source_kind, speaker_person_id, confidence, valid_from, valid_to, recorded_at, "
            "superseded_by) values (?,?,?,?,?,?,?,?,?,?,?,?)",
            (slot["subject_kind"], slot["subject_id"], slot["predicate_id"],
             cand.get("object"), cand.get("object_norm"), cand["source_kind"],
             self.person_for_cluster(cand.get("speaker_cluster")), 1.0, vf,
             new_row.get("valid_to"), new_row["recorded_at"], new_row.get("superseded_by")))
        row_id = cur.lastrowid
        # Only CURRENT facts are searchable: a superseded belief must not answer a question.
        if new_row.get("valid_to") is None:
            self.conn.execute(
                "insert into fact_fts (rowid, text) values (?,?)",
                (row_id, self._fact_fts_text(slot["subject_id"], slot["subject_kind"],
                                             slot["predicate_id"], cand.get("object"))))
        return row_id

    def _fact_index_text(self, row_id):
        """Re-render exactly what was indexed for a fact. A contentless FTS5 table keeps no copy of
        the text, so a delete has to hand it back - see _fts_delete."""
        r = self.conn.execute(
            "select subject_kind, subject_id, predicate_id, object_text from fact where id=?",
            (row_id,)).fetchone()
        if r is None:
            return None
        return self._fact_fts_text(r["subject_id"], r["subject_kind"],
                                   r["predicate_id"], r["object_text"])

    def _fts_delete(self, fts_table, rowid, text):
        """Remove a row from a contentless FTS5 index.

        `content=''` means the index stores no copy of the text, which is the property that lets a
        purge destroy words rather than orphan them - but it also means a plain DELETE is refused
        and the original text must be handed back through the special 'delete' command. That
        command has been part of FTS5 since it shipped, so this works on any build that has FTS5 at
        all; `contentless_delete=1` would be tidier and needs SQLite 3.43+, a floor this store does
        not want to carry.
        """
        if text is None:
            return
        self.conn.execute(
            f"insert into {fts_table} ({fts_table}, rowid, text) values ('delete', ?, ?)",
            (rowid, text))

    def _close_row(self, table, row_id, valid_to, winner_id):
        if table == "fact":
            # Read the indexed text BEFORE the row changes, then drop it from the lane: a
            # superseded belief must not answer a question.
            self._fts_delete("fact_fts", row_id, self._fact_index_text(row_id))
        self.conn.execute(
            f"update {table} set valid_to=?, superseded_by=? where id=?",
            (valid_to, winner_id, row_id))

    def ingest(self, cand: dict) -> dict:
        """Validate, route, decide and apply — one transaction, audit rows included."""
        recorded_at = _now_iso()
        span_cluster = self._span_clusters(cand.get("span_ids") or [])
        if not is_known(cand.get("predicate_id", "")):
            aid = self._audit("reject", "candidate", rule="registry",
                              note=f"unknown predicate {cand.get('predicate_id')!r}")
            self.conn.commit()
            return {"outcome": "reject", "reason": "unknown predicate", "row_id": None,
                    "closed": [], "audit_ids": [aid], "table": None}
        ok, reason = validate(cand, span_cluster)
        if not ok:
            aid = self._audit("reject", "candidate", rule="registry", note=str(reason))
            self.conn.commit()
            return {"outcome": "reject", "reason": reason, "row_id": None,
                    "closed": [], "audit_ids": [aid], "table": None}

        table = self.route(cand["predicate_id"])
        slot = self._slot(table, cand)
        existing = self.current(table, **slot)
        for e in existing:                       # give decide the value key it compares on
            e["object_norm"] = self._value_key(table, e)

        vkey = self._cand_value_key(table, cand)
        multi = arity(cand["predicate_id"]) == "multi"
        merge_target = None
        if multi and not cand.get("ended"):
            merge_target = next((e for e in existing if e["object_norm"] == vkey), None)

        closed, audit_ids = [], []
        with self.conn:
            if merge_target is not None:
                # Evidence accrual (see the module docstring) - the spans land on the row that
                # already holds this belief, and R5 recomputes from them.
                row_id = merge_target["id"]
                self._link_spans(table, row_id, cand.get("span_ids"), "support")
                self._link_spans(table, row_id, cand.get("contradicts"), "contradict")
                outcome = "coexist"
            else:
                # decide compares object_norm to tell coexisting values apart, so the candidate
                # has to be presented on the same footing as the existing rows: for a preference
                # the value is (topic, polarity), for an edge it is the relation. The ORIGINAL
                # cand still supplies every stored column - _insert_row reads it, not this copy.
                cand_for_decide = dict(cand)
                cand_for_decide["object_norm"] = vkey
                r = decide(cand_for_decide, existing, recorded_at)
                outcome = r["outcome"]
                row_id = None
                if r["new_row"] is not None:
                    row_id = self._insert_row(table, cand, r["new_row"], slot)
                    self._link_spans(table, row_id, cand.get("span_ids"), "support")
                    self._link_spans(table, row_id, cand.get("contradicts"), "contradict")
                for c in r["close"]:
                    self._close_row(table, c["row_id"], c["valid_to"], row_id)
                    closed.append(c["row_id"])
                for a in r["audit"]:
                    loser = row_id if a["loser"] == "new" else a["loser"]
                    audit_ids.append(self._audit(a["op"], table, loser_id=loser,
                                                 winner_id=(a["loser"] == "new") and
                                                 r["new_row"].get("superseded_by") or row_id,
                                                 rule=a["rule"], note=a.get("note")))
        conf = self.recompute_confidence(table, row_id) if row_id else None
        return {"outcome": outcome, "row_id": row_id, "closed": closed,
                "audit_ids": audit_ids, "reason": None, "table": table, "confidence": conf}

    # ----------------------------------------------------------------- R5
    def recompute_confidence(self, table, row_id) -> float:
        """R5 — from the row's spans, every time. Never reads an earlier confidence."""
        link, col = SPAN_LINK[table]
        row = self.conn.execute(f"select source_kind from {table} where id=?", (row_id,)).fetchone()
        if row is None:
            return 0.0
        if str(row[0]).startswith("stated"):
            self.conn.execute(f"update {table} set confidence=1.0 where id=?", (row_id,))
            self.conn.commit()
            return 1.0
        rows = self.conn.execute(
            f"select s.said_at, l.role from {link} l join span s on s.id=l.span_id "
            f"where l.{col}=?", (row_id,)).fetchall()
        sup = [r[0] for r in rows if r[1] == "support"]
        con = [r[0] for r in rows if r[1] == "contradict"]
        c = _confidence(distinct_days(sup), distinct_days(con))
        self.conn.execute(f"update {table} set confidence=? where id=?", (c, row_id))
        self.conn.commit()
        return c

    # ----------------------------------------------------------------- R7
    def purge_cluster(self, cluster_id) -> dict:
        """The owner's purge — the only delete. Rows resting only on this cluster's spans go with
        them; rows that also rest on surviving spans are KEPT and recomputed."""
        span_ids = {r[0] for r in self.conn.execute(
            "select id from span where cluster_id=?", (cluster_id,)).fetchall()}
        derived = []
        for table in BELIEF_TABLES + ("event",):
            link, col = SPAN_LINK[table]
            rows = self.conn.execute(
                f"select {col} rid, span_id from {link}").fetchall()
            by_row = {}
            for rid, sid in rows:
                by_row.setdefault(rid, set()).add(sid)
            derived.extend({"table": table, "row_id": rid, "span_ids": s}
                           for rid, s in sorted(by_row.items()))
        plan = _people.purge_plan(span_ids, derived)

        deleted, recomputed, audit_ids = {}, {}, []
        with self.conn:
            for table, row_id in plan["delete"]:
                link, col = SPAN_LINK[table]
                if table == "fact":
                    self._fts_delete("fact_fts", row_id, self._fact_index_text(row_id))
                self.conn.execute(f"delete from {link} where {col}=?", (row_id,))
                self.conn.execute(f"delete from {table} where id=?", (row_id,))
                deleted[table] = deleted.get(table, 0) + 1
                audit_ids.append(self._audit(
                    "purge", table, loser_id=row_id, rule="R7",
                    note=f"every supporting span belonged to cluster {cluster_id}"))
            for table, row_id in plan["recompute"]:
                link, col = SPAN_LINK[table]
                marks = ",".join("?" for _ in span_ids) or "NULL"
                self.conn.execute(
                    f"delete from {link} where {col}=? and span_id in ({marks})",
                    (row_id, *span_ids))
                recomputed[table] = recomputed.get(table, 0) + 1
                audit_ids.append(self._audit(
                    "purge", table, loser_id=row_id, rule="R7",
                    note=f"some supporting spans belonged to cluster {cluster_id}; row kept"))
            for sid in span_ids:
                r = self.conn.execute("select text from span where id=?", (sid,)).fetchone()
                self._fts_delete("span_fts", sid, r[0] if r else None)
                self.conn.execute("delete from span where id=?", (sid,))
            self.conn.execute("update cluster set n_spans=0, days_heard=0 where id=?", (cluster_id,))
            audit_ids.append(self._audit(
                "purge", "cluster", loser_id=cluster_id, rule="R7",
                note=f"owner purge of cluster {cluster_id}: {len(span_ids)} spans"))
        for table in BELIEF_TABLES:
            for _t, row_id in plan["recompute"]:
                if _t == table:
                    self.recompute_confidence(table, row_id)
        return {"spans": len(span_ids), "deleted": deleted, "recomputed": recomputed,
                "audit_ids": audit_ids}

    # ------------------------------------------------------------- the walker
    def audit_violations(self) -> list:
        """Every closed or superseded belief row must be named as a loser by an audit row.

        A purged row cannot appear here because it no longer exists - which is the point: the
        audit trail explains every row that is still in the store but no longer current.
        """
        out = []
        for table in BELIEF_TABLES:
            rows = self.conn.execute(
                f"select id from {table} where valid_to is not null or superseded_by is not null"
            ).fetchall()
            for (row_id,) in rows:
                n = self.conn.execute(
                    "select count(*) from audit where target_table=? and loser_id=? "
                    "and op in ('supersede','close','purge')", (table, row_id)).fetchone()[0]
                if n == 0:
                    out.append({"table": table, "row_id": row_id,
                                "why": "closed or superseded with no audit row naming it"})
        return out

    # ------------------------------------------------------------- the query
    def _fts_match(self, text) -> str:
        toks = _tokens(text)
        return " OR ".join(f'"{t}"' for t in toks) if toks else ""

    def _newest_now(self) -> str:
        r = self.conn.execute("select max(said_at) from span").fetchone()
        return r[0] or _now_iso()

    def query(self, text, k=5, now=None) -> list:
        """The full-text lane plus the ranker, with the spans always attached.

        `lane_score` is 1/(1+position) within each lane, positions taken in bm25 order - a bounded,
        monotone score, so the ranker's source, recency and confidence weights decide between two
        lanes rather than being swamped by raw bm25 magnitudes.

        `now` defaults to the newest span in the store, so a query is deterministic and a benchmark
        can ask what was believed on a given day.
        """
        match = self._fts_match(text)
        if not match:
            return []
        now = now or self._newest_now()
        rows = []

        fact_rows = self.conn.execute(
            "select f.id, f.object_text, f.source_kind, f.confidence, f.recorded_at, "
            "       f.subject_kind, f.subject_id, f.predicate_id "
            "from fact_fts j join fact f on f.id = j.rowid "
            "where fact_fts match ? and f.valid_to is null order by bm25(fact_fts) limit 50",
            (match,)).fetchall()
        for i, r in enumerate(fact_rows):
            rows.append({"table": "fact", "row_id": r["id"], "lane_score": 1.0 / (1 + i),
                         "source_kind": r["source_kind"], "confidence": r["confidence"],
                         "newest_span_at": self._newest_span_at("fact", r["id"]),
                         "recorded_at": r["recorded_at"],
                         "text": self._fact_fts_text(r["subject_id"], r["subject_kind"],
                                                     r["predicate_id"], r["object_text"])})

        span_rows = self.conn.execute(
            "select s.id, s.text, s.said_at from span_fts j join span s on s.id = j.rowid "
            "where span_fts match ? order by bm25(span_fts) limit 50", (match,)).fetchall()
        for i, r in enumerate(span_rows):
            # A span is evidence, not a claim: it ranks as inferred and it decays, so a stated
            # fact outranks the raw utterance it was extracted from.
            rows.append({"table": "span", "row_id": r["id"], "lane_score": 1.0 / (1 + i),
                         "source_kind": "inferred", "confidence": 1.0,
                         "newest_span_at": r["said_at"], "recorded_at": r["said_at"],
                         "text": r["text"]})

        ranked = _retrieve.rank(rows, now)[:k]
        for r in ranked:
            r["span_ids"], r["spans"] = self._spans_for(r["table"], r["row_id"])
        return ranked

    def _newest_span_at(self, table, row_id):
        link, col = SPAN_LINK[table]
        r = self.conn.execute(
            f"select max(s.said_at) from {link} l join span s on s.id=l.span_id "
            f"where l.{col}=? and l.role='support'", (row_id,)).fetchone()
        return r[0]

    def _spans_for(self, table, row_id):
        if table == "span":
            r = self.conn.execute("select id, text, said_at from span where id=?",
                                  (row_id,)).fetchone()
            return ([row_id], [{"id": r["id"], "text": r["text"], "said_at": r["said_at"]}]) \
                if r else ([], [])
        link, col = SPAN_LINK[table]
        rows = self.conn.execute(
            f"select s.id, s.text, s.said_at from {link} l join span s on s.id=l.span_id "
            f"where l.{col}=? and l.role='support' order by s.said_at", (row_id,)).fetchall()
        return ([r["id"] for r in rows],
                [{"id": r["id"], "text": r["text"], "said_at": r["said_at"]} for r in rows])
