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
from . import embed as _embed
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


# The tokeniser lives in retrieve.py from MS0.1 and is re-exported here for the callers that had
# it as `store._tokens`. ONE tokeniser, not two: the words that steer the predicate hint must be
# exactly the words that reach the FTS5 MATCH, or the lane could be narrowed by a word the index
# never sees.
_tokens = _retrieve.tokens


class MemoryStore:
    """The household store. `path` None uses JARVIS_MEMORY_HOME (else %USERPROFILE%\\.jarvis\\
    memory\\household.sqlite); ':memory:' is accepted for tests and the benchmark."""

    def __init__(self, path=None, drop_stopwords=True):
        # `drop_stopwords` carries design rule 3 (MS1a.2) per STORE, never as a module switch: the
        # A / A-prime comparison runs both settings side by side in one process, so a global would
        # make the two arms share state and the comparison meaningless.
        self.drop_stopwords = bool(drop_stopwords)
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
            row_id = cur.lastrowid
            # Only CURRENT preferences are searchable, exactly as for facts.
            if new_row.get("valid_to") is None:
                self.conn.execute(
                    "insert into pref_fts (rowid, text) values (?,?)",
                    (row_id, self._pref_fts_text(slot["person_id"], cand.get("polarity"),
                                                 slot["topic_norm"])))
            return row_id
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

    def _pref_fts_text(self, person_id, polarity, topic_norm) -> str:
        """What a preference looks like in the full-text lane: '<name> prefers <polarity> <topic>'.

        The polarity is in the text on purpose - 'likes' and 'dislikes' on one topic are two
        coexisting rows, and a reader of a search result has to be able to tell them apart.
        """
        return " ".join(x for x in (self.display_name(person_id), "prefers",
                                    str(polarity or ""), str(topic_norm or "")) if x)

    def _pref_index_text(self, row_id):
        """Re-render what was indexed for a preference, for the contentless delete."""
        r = self.conn.execute(
            "select person_id, polarity, topic_norm from preference where id=?",
            (row_id,)).fetchone()
        if r is None:
            return None
        return self._pref_fts_text(r["person_id"], r["polarity"], r["topic_norm"])

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

    def _drop_from_index(self, table, row_id):
        """Remove a row from its full-text index, but ONLY if it is still in it.

        A closed row left the index at close time. FTS5's 'delete' command on a contentless table
        does not tolerate being asked twice: deleting a rowid that is not present corrupts the
        index outright (`database disk image is malformed` on the next read), so the valid_to test
        is a correctness requirement, not a saving. Found at MS0.1 by purging a cluster whose
        preference had already been ended; the same hazard was latent for facts since MS0, where no
        test had yet purged a superseded one.
        """
        row = self.conn.execute(f"select valid_to from {table} where id=?", (row_id,)).fetchone()
        if row is None or row[0] is not None:
            return
        if table == "fact":
            self._fts_delete("fact_fts", row_id, self._fact_index_text(row_id))
        elif table == "preference":
            self._fts_delete("pref_fts", row_id, self._pref_index_text(row_id))
        # MS1a: the vector lane leaves with the full-text one. A stale embedding would
        # otherwise keep answering by meaning after the belief stopped being current.
        self.conn.execute("delete from embedding where owner_table=? and owner_id=?",
                          (table, row_id))

    def _close_row(self, table, row_id, valid_to, winner_id):
        # One path out of both indexes (MS1a): _drop_from_index checks valid_to is still null,
        # which it is here because the UPDATE below has not run yet.
        self._drop_from_index(table, row_id)
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
                self._drop_from_index(table, row_id)
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
                self.conn.execute(
                    "delete from embedding where owner_table='span' and owner_id=?", (sid,))
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

    # ------------------------------------------------------- the vector lane
    def _embed_rendering(self, table, row_id):
        """The SAME text the full-text index holds, so the two lanes describe one thing."""
        if table == "fact":
            return self._fact_index_text(row_id)
        if table == "preference":
            return self._pref_index_text(row_id)
        r = self.conn.execute("select text from span where id=?", (row_id,)).fetchone()
        return r[0] if r else None

    def embed_pending(self, embedder) -> dict:
        """Embed every current belief and every span that has no vector for this model yet.

        Deliberately NOT called from `ingest`: the write path stays embedding-free, so the p99
        write band measures the store and never a GPU. A benchmark or a batch job calls this.
        """
        import time as _time
        t0 = _time.perf_counter()
        counts = {"fact": 0, "preference": 0, "span": 0}
        model = embedder.model_id
        for table in ("fact", "preference", "span"):
            if table == "span":
                sql = ("select s.id from span s left join embedding e "
                       "on e.owner_table=? and e.owner_id=s.id and e.model=? where e.owner_id is null")
            else:
                sql = (f"select t.id from {table} t left join embedding e "
                       "on e.owner_table=? and e.owner_id=t.id and e.model=? "
                       "where e.owner_id is null and t.valid_to is null")
            ids = [r[0] for r in self.conn.execute(sql, (table, model)).fetchall()]
            pending = [(i, self._embed_rendering(table, i)) for i in ids]
            pending = [(i, t) for i, t in pending if t]
            for start in range(0, len(pending), 64):
                chunk = pending[start:start + 64]
                vecs = embedder.embed([t for _, t in chunk])
                for (row_id, _), vec in zip(chunk, vecs):
                    self.conn.execute(
                        "insert or replace into embedding (owner_table, owner_id, model, dim, vec) "
                        "values (?,?,?,?,?)",
                        (table, row_id, model, len(vec), _embed.pack(vec)))
                    counts[table] += 1
            self.conn.commit()
        counts["seconds"] = round(_time.perf_counter() - t0, 3)
        return counts

    def _vector_lane(self, query_text, embedder, limit=50,
                     tables=("fact", "preference", "span"), instruction=False):
        """Cosine over every CURRENT embedded row for this model. Never hint-restricted (design
        §6): a question the rule mis-hinted must still reach its row by meaning.

        `tables` narrows the scan - MS1a.3 runs a second lane over `("preference",)` alone - and
        `instruction` selects the query form. The two are used together: the preference lane is the
        asymmetric case, so it is the only lane embedded with the instruction prefix, and the mixed
        lane keeps the symmetric form. Two query forms are never mixed inside ONE cosine ordering.
        """
        model = embedder.model_id
        rows, meta = [], {}
        for table in tables:
            if table == "span":
                sql = ("select e.owner_id, e.dim, e.vec from embedding e "
                       "join span s on s.id = e.owner_id where e.owner_table=? and e.model=?")
            else:
                sql = (f"select e.owner_id, e.dim, e.vec from embedding e "
                       f"join {table} t on t.id = e.owner_id "
                       "where e.owner_table=? and e.model=? and t.valid_to is null")
            for r in self.conn.execute(sql, (table, model)).fetchall():
                key = (table, r[0])
                rows.append((key, _embed.unpack(r[2], r[1])))
                meta[key] = table
        if not rows:
            return []
        q = embedder.embed_query(query_text, instruction=instruction)
        return _embed.topk(q, rows, limit)

    # ------------------------------------------------------------- the query
    def _fts_match(self, text) -> str:
        """The FTS5 MATCH for a question (design §6, MS1a.2 rule 3, per-store switch).

        `self.drop_stopwords` decides whether the registry's function words reach the index; the
        decision belongs to `retrieve.query_terms`, so the two arms of the A / A-prime measurement
        differ in one flag rather than in two code paths. Either way `predicate_hint` still matches
        its own vocabulary (disjoint from STOPWORDS by assertion) and the vector lane never sees
        this, so a query left empty here still reaches its row by meaning.
        """
        toks = _retrieve.query_terms(text, self.drop_stopwords)
        return " OR ".join(f'"{t}"' for t in toks) if toks else ""

    def _named_persons(self, text) -> set:
        """The ids of the known persons this question NAMES (design §6, MS1a.4 the subject gate).

        A person is named when EVERY token of its normalised `display_name` appears among the
        question's tokens. The tokeniser's raw output is used, BEFORE stopword removal - a name is
        never a function word, and gating on a name the full-text query had already dropped would
        be a silent no-op. An unnamed or unknown name yields the empty set, and the gate then does
        nothing at all.

        Exact tokens only: aliases and nicknames belong to MS2's people layer, and this limit is
        stated rather than approximated. The store never GUESSES a subject - a known entity selects
        the candidate set, the K-b instinct one level up.
        """
        qt = set(_tokens(text))
        if not qt:
            return set()
        out = set()
        for r in self.conn.execute(
                "select id, display_name from person where display_name is not null").fetchall():
            nt = _tokens(r["display_name"])
            if nt and all(t in qt for t in nt):
                out.add(r["id"])
        return out

    @staticmethod
    def _subject_gate(members, named):
        """Drop, from every lane, belief rows about a DIFFERENT known person.

        Only rows that CARRY a person (a person-subject fact, a preference) are eligible; spans and
        household/topic facts have `person_id` None and are never gated - evidence is not a claim
        about anybody, and gating it would hide the utterance a belief rests on. With `named` empty
        nothing is dropped. A lane the gate empties is omitted rather than left as an empty lane.

        The measured reason is MS1a.3's growth trace: all six misses were a filler fact about
        ANOTHER person out-summing the answer, which stood at fts_fact rank 1.
        """
        if not named:
            return members
        out = {}
        for lane_name, lane in (members or {}).items():
            kept = [m for m in lane
                    if m.get("person_id") is None or m["person_id"] in named]
            if kept:
                out[lane_name] = kept
        return out

    def _newest_now(self) -> str:
        r = self.conn.execute("select max(said_at) from span").fetchone()
        return r[0] or _now_iso()

    def query(self, text, k=5, now=None, predicate_hint="auto", embedder=None) -> list:
        """The retrieval lanes, fused, with the spans always attached.

        Lanes: full-text over facts, preferences and spans (the first two hint-restricted as at
        MS0.1), and — when an `embedder` is given — a vector lane over the same rows' embeddings.
        The lanes are combined by RECIPROCAL RANK (`retrieve.fuse`, K = 60): only their ORDER is
        comparable, since BM25 is negative-better and unbounded while cosine is bounded, and a row
        both lanes like outranks a row one lane loves.

        **The hint restricts the full-text lane only** (design §6, decided at MS1a): MS0.1 measured
        two scenario questions hinting to the wrong predicate, and an unrestricted vector lane is
        what lets such a question still reach its row by meaning.

        `now` defaults to the newest span in the store, so a query is deterministic and a benchmark
        can ask what was believed on a given day.
        """
        match = self._fts_match(text)
        now = now or self._newest_now()
        hint = _retrieve.predicate_hint(text) if predicate_hint == "auto" else predicate_hint
        want_prefs = hint is None or hint == PREFERENCE_PREDICATE
        want_facts = hint is None or hint != PREFERENCE_PREDICATE

        members = {}          # lane -> [member dicts, in the lane's raw order]
        info = {}             # key -> the row's metadata

        def note(key, table, row_id, source_kind, confidence, recorded_at, text_, newest_span_at,
                 person_id=None):
            # `person_id` is the subject gate's input and is READ FROM THE COLUMNS - a fact's
            # subject_id when subject_kind is 'person', a preference's person_id, None for a span
            # and for a household/topic fact. It is never inferred from the rendered text.
            if key not in info:
                info[key] = {"key": key, "table": table, "row_id": row_id,
                             "source_kind": source_kind, "confidence": confidence,
                             "recorded_at": recorded_at, "text": text_,
                             "newest_span_at": newest_span_at, "person_id": person_id}
            return info[key]

        if match:
            if want_facts:
                sql = ("select f.id, f.object_text, f.source_kind, f.confidence, f.recorded_at, "
                       "       f.subject_kind, f.subject_id, f.predicate_id "
                       "from fact_fts j join fact f on f.id = j.rowid "
                       "where fact_fts match ? and f.valid_to is null")
                args = [match]
                if hint is not None:
                    sql += " and f.predicate_id = ?"
                    args.append(hint)
                sql += " order by bm25(fact_fts) limit 50"
                lane = []
                for r in self.conn.execute(sql, tuple(args)).fetchall():
                    key = ("fact", r["id"])
                    lane.append(note(key, "fact", r["id"], r["source_kind"], r["confidence"],
                                     r["recorded_at"],
                                     self._fact_fts_text(r["subject_id"], r["subject_kind"],
                                                         r["predicate_id"], r["object_text"]),
                                     self._newest_span_at("fact", r["id"]),
                                     person_id=(r["subject_id"]
                                                if r["subject_kind"] == "person" else None)))
                members["fts_fact"] = lane

            if want_prefs:
                lane = []
                for r in self.conn.execute(
                        "select p.id, p.person_id, p.polarity, p.topic_norm, p.source_kind, "
                        "       p.confidence, p.recorded_at "
                        "from pref_fts j join preference p on p.id = j.rowid "
                        "where pref_fts match ? and p.valid_to is null "
                        "order by bm25(pref_fts) limit 50", (match,)).fetchall():
                    key = ("preference", r["id"])
                    lane.append(note(key, "preference", r["id"], r["source_kind"], r["confidence"],
                                     r["recorded_at"],
                                     self._pref_fts_text(r["person_id"], r["polarity"],
                                                         r["topic_norm"]),
                                     self._newest_span_at("preference", r["id"]),
                                     person_id=r["person_id"]))
                members["fts_pref"] = lane

            lane = []
            for r in self.conn.execute(
                    "select s.id, s.text, s.said_at from span_fts j join span s on s.id = j.rowid "
                    "where span_fts match ? order by bm25(span_fts) limit 50", (match,)).fetchall():
                key = ("span", r["id"])
                # A span is evidence, not a claim: it ranks as inferred and it decays.
                lane.append(note(key, "span", r["id"], "inferred", 1.0, r["said_at"], r["text"],
                                 r["said_at"]))
            members["fts_span"] = lane

        cos_by_key = {}
        cos_pref_by_key = {}
        if embedder is not None:
            lane = []
            for key, cos in self._vector_lane(text, embedder):
                cos_by_key[key] = cos
                if key not in info:
                    self._note_from_db(key, note)
                if key in info:
                    lane.append(info[key])
            members["vec"] = lane

            # MS1a.3: the PREFERENCE model gets its own lane, ordered by the INSTRUCTION-prefixed
            # query. Scenario-to-preference is the asymmetric case the instruction form exists for,
            # and MS1a.2 measured the cost of applying it to every table (the update band, 93.75 %).
            # The rank restart this lane brings is bounded to one table of a handful of rows, and
            # its price - a preference among a fact question's five results - is MEASURED
            # (`pref_in_top5_rate`), never assumed away. Omitted entirely when empty.
            pref_lane = []
            for key, cos in self._vector_lane(text, embedder, tables=("preference",),
                                              instruction=True):
                cos_pref_by_key[key] = cos
                if key not in info:
                    self._note_from_db(key, note)
                if key in info:
                    pref_lane.append(info[key])
            if pref_lane:
                members["vec_pref"] = pref_lane

        # MS1a.4 THE SUBJECT GATE, applied after every lane is collected and BEFORE any ordering,
        # so a gated row never occupies a rank: a question that names a known person must not take
        # another person's belief. `info` keeps the rows; only lane membership changes.
        members = self._subject_gate(members, self._named_persons(text))

        if not members:
            return []

        # (1) each lane orders its OWN candidates by the weighted score, then (2) the lanes are
        # fused by reciprocal rank over those orderings - the final relevance, multiplied by nothing.
        lanes, best_w = {}, {}
        for lane_name, lane_members in members.items():
            ordered = _retrieve.lane_order(lane_members, now)
            lanes[lane_name] = [m["key"] for m in ordered]
            for m in ordered:
                mk = m["key"]          # not `k` - that is the caller's result limit
                if m["wscore"] > best_w.get(mk, float("-inf")):
                    best_w[mk] = m["wscore"]

        # (2) MS1a.2: the lanes merge by reciprocal rank with each ROW weighed by its claim status
        # - beliefs 1.0, spans the inferred source rank - because R2 orders only WITHIN a lane and a
        # fact and the span it came from are never in the same one. A key W_CLAIM does not name
        # weighs 1.0 (see `fuse`), so an unclassified table sinks nothing.
        row_weights = {key: _retrieve.W_CLAIM.get(key[0], 1.0) for key in info}
        fused = _retrieve.fuse(lanes, row_weights)
        lane_ranks = {}
        for lane_name, keys in lanes.items():
            for i, key in enumerate(keys, start=1):
                lane_ranks.setdefault(key, {})[lane_name] = i

        rows = []
        for key, relevance in fused.items():
            meta = info.get(key)
            if meta is None:
                continue
            row = dict(meta)
            row.pop("key", None)
            row["relevance"] = relevance
            row["tiebreak"] = best_w.get(key, 0.0)
            row["lanes"] = lane_ranks.get(key, {})
            if key in cos_by_key:
                row["cos"] = cos_by_key[key]
            if key in cos_pref_by_key:
                # the preference lane's own cosine, kept apart: it is measured against a DIFFERENT
                # query form, so it must never be compared with `cos` or substituted for it.
                row["cos_pref"] = cos_pref_by_key[key]
            rows.append(row)

        # MS1a.2 EVIDENCE COLLAPSE: a span that is an evidence span of a belief already in the
        # fused set is not a second result - the belief carries it in `spans`. The utterance and the
        # fact extracted from it must never compete (design §6; the measured reason is MS1a §3.8).
        # Done BEFORE the top-k cut, so the answer set fills from the survivors rather than spending
        # a slot on a row that is already attached to the row above it. A span no belief stands on
        # is untouched and stays a result.
        spans_cache = {}
        evidence_span_ids = set()
        for r in rows:
            if r["table"] in ("fact", "preference"):
                got = self._spans_for(r["table"], r["row_id"])
                spans_cache[(r["table"], r["row_id"])] = got
                evidence_span_ids.update(got[0])
        if evidence_span_ids:
            rows = [r for r in rows
                    if not (r["table"] == "span" and r["row_id"] in evidence_span_ids)]

        ranked = _retrieve.rank(rows, now)[:k]
        for r in ranked:
            got = spans_cache.get((r["table"], r["row_id"]))
            if got is None:
                got = self._spans_for(r["table"], r["row_id"])
            r["span_ids"], r["spans"] = got
        return ranked

    def _note_from_db(self, key, note):
        """Fill a row's metadata when the vector lane found it and no full-text lane did.

        Re-checks `valid_to is null` even though `_vector_lane` already joins on it: a closed row
        reappearing by meaning is exactly the failure the embedding lifecycle exists to prevent,
        and this is the one path that reaches a row the full-text lanes never saw.
        """
        table, row_id = key
        if table == "fact":
            r = self.conn.execute(
                "select object_text, source_kind, confidence, recorded_at, subject_kind, "
                "subject_id, predicate_id from fact where id=? and valid_to is null",
                (row_id,)).fetchone()
            if r:
                note(key, "fact", row_id, r["source_kind"], r["confidence"], r["recorded_at"],
                     self._fact_fts_text(r["subject_id"], r["subject_kind"], r["predicate_id"],
                                         r["object_text"]),
                     self._newest_span_at("fact", row_id),
                     person_id=(r["subject_id"] if r["subject_kind"] == "person" else None))
        elif table == "preference":
            r = self.conn.execute(
                "select person_id, polarity, topic_norm, source_kind, confidence, recorded_at "
                "from preference where id=? and valid_to is null", (row_id,)).fetchone()
            if r:
                note(key, "preference", row_id, r["source_kind"], r["confidence"],
                     r["recorded_at"],
                     self._pref_fts_text(r["person_id"], r["polarity"], r["topic_norm"]),
                     self._newest_span_at("preference", row_id),
                     person_id=r["person_id"])
        else:
            r = self.conn.execute("select text, said_at from span where id=?", (row_id,)).fetchone()
            if r:
                note(key, "span", row_id, "inferred", 1.0, r["said_at"], r["text"], r["said_at"])

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
