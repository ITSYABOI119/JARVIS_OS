"""The DDL — the design's §3 data model, one SQLite file.

Every table is append-only except for four documented set-once-later columns: `valid_to` and
`superseded_by` on the versioned tables, `deleted_audio_at` on a recording, and `person_id` on a
cluster. Row ids are monotonic integers and no row is ever renumbered, so an audit row that names a
loser by id keeps naming the same row for the life of the store.

`fact_fts` and `span_fts` are contentless FTS5 tables (`content=''`): the index holds no copy of the
text, so a purge that deletes a span cannot leave its words behind in a shadow table. The store
maintains both by hand on write and on purge — the price of that guarantee is that the delete must
be explicit, which is why `purge_cluster` removes the FTS rows itself rather than trusting a trigger.

The porter tokenizer is what lets "where does sam live" reach a fact whose text says "lives in".

`embedding` and `style_snapshot` exist but are unused at MS0: the embedding lane lands at MS1 with
the extractor that shares its GPU venue, and style is derived at MS2. They are created now so the
schema does not change under a store that already holds data.
"""

DDL = """
PRAGMA foreign_keys = ON;

-- ---------------------------------------------------------------- the spine
CREATE TABLE IF NOT EXISTS recording (
    id                INTEGER PRIMARY KEY,
    sha256            TEXT NOT NULL,
    started_at        TEXT NOT NULL,          -- UTC ISO 8601
    duration_s        REAL,
    device            TEXT,
    transcribed_at    TEXT,
    deleted_audio_at  TEXT                    -- set once, when the audio is destroyed
);

CREATE TABLE IF NOT EXISTS cluster (
    id           INTEGER PRIMARY KEY,
    centroid     BLOB,                        -- 192-d ECAPA, written at MS2
    n_spans      INTEGER NOT NULL DEFAULT 0,
    first_heard  TEXT,
    days_heard   INTEGER NOT NULL DEFAULT 0,
    person_id    INTEGER REFERENCES person(id)   -- set once, when the cluster earns personhood
);

CREATE TABLE IF NOT EXISTS span (
    id                INTEGER PRIMARY KEY,
    recording_id      INTEGER NOT NULL REFERENCES recording(id),
    t_start_s         REAL NOT NULL,
    t_end_s           REAL NOT NULL,
    cluster_id        INTEGER REFERENCES cluster(id),
    text              TEXT NOT NULL,          -- verbatim ASR output; never edited, only purged
    asr_conf          REAL,
    said_at           TEXT NOT NULL,          -- dialogue time = recording.started_at + t_start_s
    about_time        TEXT,                   -- occurrence time, when it is known
    about_time_source TEXT                    -- 'stated' | 'extractor' | NULL
);

-- ------------------------------------------------------------ the three layers
CREATE TABLE IF NOT EXISTS person (
    id                INTEGER PRIMARY KEY,
    kind              TEXT NOT NULL,          -- 'owner' | 'cluster'
    display_name      TEXT,
    name_confidence   REAL,
    name_source_kind  TEXT,
    created_at        TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS event (
    id          INTEGER PRIMARY KEY,
    text        TEXT NOT NULL,
    about_time  TEXT,
    cluster_id  INTEGER REFERENCES cluster(id),
    created_at  TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS event_span (
    event_id  INTEGER NOT NULL REFERENCES event(id),
    span_id   INTEGER NOT NULL,
    role      TEXT NOT NULL DEFAULT 'support',
    PRIMARY KEY (event_id, span_id, role)
);

CREATE TABLE IF NOT EXISTS fact (
    id                 INTEGER PRIMARY KEY,
    subject_kind       TEXT NOT NULL,         -- 'person' | 'household' | 'topic'
    subject_id         INTEGER,
    predicate_id       TEXT NOT NULL,
    object_text        TEXT,
    object_norm        TEXT,
    source_kind        TEXT NOT NULL,
    speaker_person_id  INTEGER REFERENCES person(id),
    confidence         REAL NOT NULL DEFAULT 1.0,
    valid_from         TEXT,
    valid_to           TEXT,                  -- set once, when the row stops being current
    recorded_at        TEXT NOT NULL,
    superseded_by      INTEGER                -- set once, pointing at the winner
);
CREATE TABLE IF NOT EXISTS fact_span (
    fact_id  INTEGER NOT NULL REFERENCES fact(id),
    span_id  INTEGER NOT NULL,
    role     TEXT NOT NULL DEFAULT 'support', -- 'support' | 'contradict'
    PRIMARY KEY (fact_id, span_id, role)
);

CREATE TABLE IF NOT EXISTS edge (
    id             INTEGER PRIMARY KEY,
    from_person    INTEGER NOT NULL REFERENCES person(id),
    to_person      INTEGER NOT NULL REFERENCES person(id),
    relation_id    TEXT NOT NULL,
    source_kind    TEXT NOT NULL,
    confidence     REAL NOT NULL DEFAULT 1.0,
    valid_from     TEXT,
    valid_to       TEXT,
    recorded_at    TEXT NOT NULL,
    superseded_by  INTEGER
);
CREATE TABLE IF NOT EXISTS edge_span (
    edge_id  INTEGER NOT NULL REFERENCES edge(id),
    span_id  INTEGER NOT NULL,
    role     TEXT NOT NULL DEFAULT 'support',
    PRIMARY KEY (edge_id, span_id, role)
);

CREATE TABLE IF NOT EXISTS preference (
    id             INTEGER PRIMARY KEY,
    person_id      INTEGER NOT NULL REFERENCES person(id),
    topic_norm     TEXT NOT NULL,
    polarity       TEXT NOT NULL,             -- likes | dislikes | wants | avoids
    strength       INTEGER,
    source_kind    TEXT NOT NULL,
    confidence     REAL NOT NULL DEFAULT 1.0,
    valid_from     TEXT,
    valid_to       TEXT,
    recorded_at    TEXT NOT NULL,
    superseded_by  INTEGER
);
CREATE TABLE IF NOT EXISTS preference_span (
    preference_id  INTEGER NOT NULL REFERENCES preference(id),
    span_id        INTEGER NOT NULL,
    role           TEXT NOT NULL DEFAULT 'support',
    PRIMARY KEY (preference_id, span_id, role)
);

CREATE TABLE IF NOT EXISTS style_snapshot (
    id           INTEGER PRIMARY KEY,
    person_id    INTEGER NOT NULL REFERENCES person(id),
    window_from  TEXT,
    window_to    TEXT,
    n_spans      INTEGER,
    stats_json   TEXT
);

-- ------------------------------------------------------------------- the audit
-- Every loss, kept. This table is the reason "nothing is deleted silently" is a property
-- rather than a promise, and audit_violations() walks it.
CREATE TABLE IF NOT EXISTS audit (
    id            INTEGER PRIMARY KEY,
    ts            TEXT NOT NULL,
    op            TEXT NOT NULL,              -- supersede | close | demote | purge | reject
    target_table  TEXT NOT NULL,
    loser_id      INTEGER,
    winner_id     INTEGER,
    rule          TEXT,                       -- R1..R7 | registry
    note          TEXT
);

CREATE TABLE IF NOT EXISTS embedding (
    owner_table  TEXT NOT NULL,
    owner_id     INTEGER NOT NULL,
    model        TEXT NOT NULL,
    dim          INTEGER NOT NULL,
    vec          BLOB NOT NULL,
    PRIMARY KEY (owner_table, owner_id, model)
);

-- --------------------------------------------------------------- the indexes
CREATE INDEX IF NOT EXISTS fact_slot
    ON fact (subject_kind, subject_id, predicate_id, valid_to);
CREATE INDEX IF NOT EXISTS edge_slot
    ON edge (from_person, to_person, relation_id, valid_to);
CREATE INDEX IF NOT EXISTS preference_slot
    ON preference (person_id, topic_norm, valid_to);
CREATE INDEX IF NOT EXISTS span_cluster ON span (cluster_id, said_at);
CREATE INDEX IF NOT EXISTS fact_span_span ON fact_span (span_id);
CREATE INDEX IF NOT EXISTS edge_span_span ON edge_span (span_id);
CREATE INDEX IF NOT EXISTS preference_span_span ON preference_span (span_id);
CREATE INDEX IF NOT EXISTS event_span_span ON event_span (span_id);
CREATE INDEX IF NOT EXISTS audit_target ON audit (target_table, loser_id);

-- ------------------------------------------------------------- the full-text lane
-- content='' keeps no copy of the text in the index, so a purge cannot leave words behind.
CREATE VIRTUAL TABLE IF NOT EXISTS fact_fts
    USING fts5(text, content='', tokenize='porter unicode61');
CREATE VIRTUAL TABLE IF NOT EXISTS span_fts
    USING fts5(text, content='', tokenize='porter unicode61');
"""
