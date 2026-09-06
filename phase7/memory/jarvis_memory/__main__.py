"""`python -m jarvis_memory` — the operator's view of the household store.

    init                      create (or open) the store and print where it lives
    ingest <candidates.json>  a JSON list of candidates, applied in order through the rules
    query <text> [--k 5]      the full-text lane plus the ranker, with the spans printed
    purge <cluster_id>        R7: the owner's purge, the only delete in the system
    audit [--limit 20]        the audit trail, newest first, plus the violation walker

Every command runs against the default store (JARVIS_MEMORY_HOME, else
%USERPROFILE%\\.jarvis\\memory\\household.sqlite) unless --db names another; ':memory:' works and is
what the tests use. Standard library only, and nothing here touches the box.

`purge` is destructive and irreversible by design, so it asks for the cluster id to be typed back
unless --yes is given. Nothing else in the store deletes anything.
"""
import argparse
import json
import sys

from .paths import default_db
from .store import MemoryStore


def _open(args):
    return MemoryStore(args.db) if args.db else MemoryStore()


def cmd_init(args) -> int:
    st = _open(args)
    n = st.conn.execute("select count(*) from fact").fetchone()[0]
    print(f"store: {st.path}")
    print(f"facts: {n}")
    print(f"spans: {st.conn.execute('select count(*) from span').fetchone()[0]}")
    return 0


def cmd_ingest(args) -> int:
    st = _open(args)
    cands = json.loads(open(args.candidates, encoding="utf-8").read())
    if not isinstance(cands, list):
        print("the candidates file must hold a JSON list", file=sys.stderr)
        return 2
    counts = {}
    for c in cands:
        r = st.ingest(c)
        counts[r["outcome"]] = counts.get(r["outcome"], 0) + 1
        if r["outcome"] == "reject":
            print(f"REJECT {r['reason']}")
    for k in sorted(counts):
        print(f"{k}: {counts[k]}")
    return 0


def cmd_query(args) -> int:
    st = _open(args)
    for i, hit in enumerate(st.query(args.text, k=args.k), 1):
        print(f"{i}. [{hit['table']} {hit['row_id']}] score={hit['score']:.4f}  {hit['text']}")
        for sp in hit["spans"][:3]:
            print(f"     evidence {sp['said_at']}  {sp['text']!r}")
    return 0


def cmd_purge(args) -> int:
    st = _open(args)
    if not args.yes:
        print(f"purge removes every span of cluster {args.cluster_id} and the beliefs that rest "
              f"only on them. This cannot be undone.")
        typed = input(f"type the cluster id ({args.cluster_id}) to confirm: ").strip()
        if typed != str(args.cluster_id):
            print("not confirmed; nothing was purged")
            return 1
    res = st.purge_cluster(args.cluster_id)
    print(json.dumps(res, indent=2, sort_keys=True))
    return 0


def cmd_audit(args) -> int:
    st = _open(args)
    rows = st.conn.execute(
        "select id, ts, op, target_table, loser_id, rule, note from audit "
        "order by id desc limit ?", (args.limit,)).fetchall()
    for r in rows:
        print(f"{r['id']:6d} {r['ts']} {r['op']:10s} {r['target_table']:11s} "
              f"loser={r['loser_id']} rule={r['rule']} {r['note'] or ''}")
    bad = st.audit_violations()
    print(f"\naudit violations: {len(bad)}")
    for b in bad:
        print(f"  {b}")
    return 1 if bad else 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="jarvis_memory", description=__doc__.split("\n")[0])
    p.add_argument("--db", default=None,
                   help=f"store path (default {default_db()}); ':memory:' is accepted")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("init").set_defaults(fn=cmd_init)

    q = sub.add_parser("ingest")
    q.add_argument("candidates")
    q.set_defaults(fn=cmd_ingest)

    q = sub.add_parser("query")
    q.add_argument("text")
    q.add_argument("--k", type=int, default=5)
    q.set_defaults(fn=cmd_query)

    q = sub.add_parser("purge")
    q.add_argument("cluster_id", type=int)
    q.add_argument("--yes", action="store_true", help="skip the typed confirmation")
    q.set_defaults(fn=cmd_purge)

    q = sub.add_parser("audit")
    q.add_argument("--limit", type=int, default=20)
    q.set_defaults(fn=cmd_audit)

    args = p.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
